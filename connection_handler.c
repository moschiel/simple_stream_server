#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <errno.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <signal.h>

#include "connection_handler.h"
#include "simple_stream_server.h"
#include "thread_list.h"

extern pthread_mutex_t file_mutex;
extern volatile sig_atomic_t keep_running;


// Receives dada from client and appends to DATA_FILE_PATH, creating this file if it doesn’t exist.
int recv_client_data_and_append_to_file(int client_sockfd)
{
    // Set a receive timeout
    struct timeval tv;
    tv.tv_sec = 1;     // 1 seconds
    tv.tv_usec = 0;
    if (setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        syslog(LOG_ERR, "setsockopt(SO_RCVTIMEO): %s", strerror(errno));
        close(client_sockfd);
        return -1;
    }
    
    int ret = -1;
    char buffer[1024];
    ssize_t bytes_read;
    int acquired_file_mutex = 0;

    #if USE_SYSCALL_FILE
    int fd = INVALID_FILE;   // file descriptor
    #else
    FILE *fd = INVALID_FILE; // file pointer
    #endif

    /* We dont read the client data at once because it can be very large, 
     * therefore we only received a total of 'sizeof(buffer)' bytes, and append it.
     * Repeat the processes in a loop until all the data has been received. */
     while(keep_running) {
        bytes_read = recv(client_sockfd, buffer, sizeof(buffer), 0);

        if (bytes_read < 0) {
            if(errno == EWOULDBLOCK || errno == EAGAIN) {
                //printf("timeout, try again\n");
                continue;
            } else {
                syslog(LOG_ERR, "recv: %s", strerror(errno));
                break;
            }
        } else if (bytes_read == 0) {
            syslog(LOG_INFO, "Connection closed by peer, socket: %u", client_sockfd);
            break;
        }
        
        // If fd = INVALID_FILE, it is the first iteration, then acquire mutex and open the file
        if (fd == INVALID_FILE) {
            // lock file_mutex to avoid writing in parallel with other threads
            pthread_mutex_lock(&file_mutex);
            acquired_file_mutex = 1;
            // open file
            #if USE_SYSCALL_FILE
            fd = open(DATA_FILE_PATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
            #else
            fd = fopen(DATA_FILE_PATH, "a");
            #endif 
                
            if (fd == INVALID_FILE) {
                syslog(LOG_ERR, "%s (recv_client_data_and_append_to_file): %s", USE_SYSCALL_FILE ? "open" : "fopen", strerror(errno));
                break;
            }
        }

        // Append data to file
        #if USE_SYSCALL_FILE
        write(fd, buffer, bytes_read);
        #else
        fwrite(buffer, 1, bytes_read, fd);
        #endif 

        // If found '\n', stop reading
        if (memchr(buffer, '\n', bytes_read)) {
            ret = 0;
            break;
        }

        memset(buffer, 0, sizeof(buffer));
    }

    #if USE_SYSCALL_FILE
    if(fd != INVALID_FILE) close(fd);
    #else
    if(fd != INVALID_FILE) fclose(fd);
    #endif 
    
    if(acquired_file_mutex)
        pthread_mutex_unlock(&file_mutex);    
    
    return ret;
}

// Returns the full content of DATA_FILE_PATH to the client as soon as the received data packet completes.
int send_file_data_to_client(int client_sockfd) {
    int ret = -1;
    char buffer[1024];
    ssize_t bytes_read;

    #if USE_SYSCALL_FILE
    int fd = open(DATA_FILE_PATH, O_RDONLY);
    #else
    FILE *fd = fopen(DATA_FILE_PATH, "r");
    #endif

    if (fd == INVALID_FILE) {
        syslog(LOG_ERR, "%s (send_file_data_to_client): %s", USE_SYSCALL_FILE ? "open" : "fopen", strerror(errno));
    } else {
        // We dont read the file at once because it can be very large, 
        // therefore we only read a total of 'sizeof(buffer)' bytes, and send it.
        // Repeat the processes in a loop until all the data has been send.
        do {
            #if USE_SYSCALL_FILE
            bytes_read = read(fd, buffer, sizeof(buffer));
            #else
            bytes_read = fread(buffer, 1, sizeof(buffer), fd);
            #endif
            if(bytes_read > 0) {
                send(client_sockfd, buffer, bytes_read, 0);
            }
        } while(bytes_read > 0);

        ret = 0;
    }

    #if USE_SYSCALL_FILE
    if(fd != INVALID_FILE) close(fd);
    #else
    if(fd != INVALID_FILE) fclose(fd);
    #endif 

    return ret;
}

/*
 * connection_handler:
 * Reads data from the client socket until an error or end-of-stream, 
 * then writes the data to file specified at DATA_FILE_PATH.
 * Uses file_mutex to avoid interleaving data from multiple clients.
 */
 void *connection_handler(void *args) {
    ThreadArgs* threadArgs = (ThreadArgs*)args;
    char ip_str[INET_ADDRSTRLEN];

    int client_sockfd = threadArgs->client_sockfd;
    strncpy(ip_str, threadArgs->ip_str, sizeof(ip_str));

    free(args);
    
    syslog(LOG_INFO, "New client connection, socket: %u (thread: %lu)", client_sockfd, pthread_self());

    /* Read data from the client socket */
    if(recv_client_data_and_append_to_file(client_sockfd) == 0) {
        /* Return the file content to the client socket */
        send_file_data_to_client(client_sockfd);
    }

    close(client_sockfd);

    syslog(LOG_INFO, "Closed connection from %s", ip_str);
    
    syslog(LOG_INFO, "Exiting thread id: %lu,", pthread_self());
    
    /* Set this thread as exited the global client thread list */
    set_thread_as_exited(pthread_self());
    
    /* Exit the thread */
    return NULL;
 }