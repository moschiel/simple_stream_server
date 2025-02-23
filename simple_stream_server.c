/*
* simple_stream_server.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>
#include <sys/time.h>
#include <pthread.h>
#include <signal.h>

#include "simple_stream_server.h"
#include "server_utils.h"
#include "thread_list.h"

#define USE_THREAD_TIMER 1
#define USE_INTERRUPT_TIMER (!USE_THREAD_TIMER)

#define PORT "9000" // the port users will be connecting to


volatile sig_atomic_t keep_running = 1; /* Flag to keep server running */
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Global mutex for file writes.

void write_timestamp() {
    /* Build the timestamp string in RFC 2822 style */
    time_t rawtime;
    struct tm tm_info;
    
    time(&rawtime);
    /* localtime_r is thread-safe version of localtime */
    localtime_r(&rawtime, &tm_info);
    
    /* Example RFC 2822-like format: "Fri, 07 Oct 2022 06:59:23 +0000" */
    char auxtimebuf[64];
    strftime(auxtimebuf, sizeof(auxtimebuf), "%a, %d %b %Y %T %z", &tm_info);
    char timebuffer[128];
    snprintf(timebuffer, sizeof(timebuffer), "timestamp:%s\n", auxtimebuf);

    syslog(LOG_INFO, "%s", timebuffer);

    /* Lock the mutex before writing to DATA_FILE_PATH */
    pthread_mutex_lock(&file_mutex);
    {
        /* Append-only open */
        #if USE_SYSCALL_FILE
        int fd = open(DATA_FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
        #else
        FILE *fd = fopen(DATA_FILE_PATH, "a");
        #endif

        if (fd != INVALID_FILE) {
            /* "timestamp: <RFC2822 time>" followed by a newline */
            #if USE_SYSCALL_FILE
            write(fd, timebuffer, strlen(timebuffer));
            close(fd);
            #else
            fwrite(timebuffer, 1, strlen(timebuffer), fd);
            fclose(fd);
            #endif
        } else {
            syslog(LOG_ERR, "%s (timer_thread_func): %s", USE_SYSCALL_FILE ? "open" : "fopen", strerror(errno));
        }
    }
    pthread_mutex_unlock(&file_mutex);
}

#if USE_THREAD_TIMER
void *timer_thread_func(void *arg) {
    (void)arg; // quiet unused variable warning
    while (keep_running) {
        
        // Wait 10 seconds
        for(int i=0; i<10 && keep_running; i++){
            sleep(1);  //sleep 1 second
            join_exited_threads(); //check if there is any exited thread to join
        }
        
        if(!keep_running) 
            break;

        //Each 10 seconds, write timestamp to file
        write_timestamp();
    }

    syslog(LOG_INFO, "Exiting 'timer' thread, tid: %lu,", pthread_self());

    set_thread_as_exited(pthread_self());

    return NULL;
}

int setup_timer_thread() {
    pthread_t tid;
    if (pthread_create(&tid, NULL, timer_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "setup_timer_thread: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    add_thread_to_list(tid);
    return 0;
}
#else
void timer_handler(int signum) {
    (void)signum; // quiet unused variable warning
    printf("timer\n");
}

int setup_timer_handler() {
    // Set up the signal handler for SIGALRM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = timer_handler;
    sigaction(SIGALRM, &sa, NULL);

    // Configure the timer to expire after 10 seconds, then every 10 seconds
    struct itimerval timer;
    timer.it_value.tv_sec = 10;      // first expiration after 10s
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 10;   // subsequent intervals of 10s
    timer.it_interval.tv_usec = 0;

    // Start the real-time timer (ITIMER_REAL)
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        syslog(LOG_ERR, "setitimer: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    return 0;
}
#endif

void signal_exit_handler(int signum) {
    (void)signum; // quiet unused variable warning
    syslog(LOG_INFO, "Caught signal, exiting");
    server_stop();
    exit(0);
}

void setup_signal_exit_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_exit_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        syslog(LOG_ERR, "Failed to fork: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid > 0) {
        // Father process, exit and let child running on background
        exit(EXIT_SUCCESS);
    }

    
    // Create a new session and detach from the terminal:
    // - The process becomes the session leader.
    // - It detaches from the controlling terminal.
    // - Ensures the daemon keeps running even if the terminal is closed.
    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Since the daemon runs in the background, it does not have a terminal for standard input/output.
    // Redirect stdin, stdout, and stderr to /dev/null to discard any unintended output or input.
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    // Verify args to detect deamon mode (-d)
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    /* Register signal handlers */
    setup_signal_exit_handlers();

    openlog(PROCESS_NAME, LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);
    
    /* Start the server on the default port */
    if(server_start(PORT) != 0) {
        syslog(LOG_ERR, "starting server FAIL!");
        return -1;
    }
    
    /* Any Thread must be created AFTER DAEMONIZATION, as the FORK process doesnt inherit threads*/
    if (daemon_mode) {
        syslog(LOG_INFO, "Running in daemon mode");
        daemonize();
    }

    /* Create Timer thread */
    #if USE_THREAD_TIMER
    setup_timer_thread();
    #else
    setup_timer_handler();
    #endif

    /* Accept connections until a signal (SIGINT/SIGTERM) stops the server */
    server_run();

    /* Stop the server gracefully (close socket, join threads, etc.) */
    server_stop();
}

