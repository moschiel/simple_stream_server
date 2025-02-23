#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>

#include "server_utils.h"
#include "simple_stream_server.h"
#include "connection_handler.h"
#include "thread_list.h"

#define SIMPLE_SERVER_START 1
#define FLEXIBLE_SERVER_START (!SIMPLE_SERVER_START)

#define BACKLOG 10   // how many pending connections queue will hold

static int server_sockfd = -1; // server socket file descriptor

extern volatile sig_atomic_t keep_running;
extern pthread_mutex_t file_mutex;
extern pthread_mutex_t thread_list_mutex;
extern pthread_t timer_thread;


#if SIMPLE_SERVER_START
/*
 * server_start: creates a socket, binds it to the specified port, 
 * and starts listening for incoming connections.
 *
 * Returns 0 on success, or -1 on error.
 */
 int server_start(char *port) {
    struct sockaddr_in server_addr;

    /* Create the socket */
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd < 0) {
        syslog(LOG_ERR, "socket: %s", strerror(errno));
        return -1;
    }

    /* Allow address reuse to avoid "Address already in use" on quick restarts */
    int optval = 1;
    setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval));

    /* Configure server address (IPv4) */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(atoi(port));
    server_addr.sin_addr.s_addr = INADDR_ANY; /* 0.0.0.0 (bind all interfaces) */

    /* Bind the socket to the specified address/port */
    if (bind(server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        syslog(LOG_ERR, "bind: %s", strerror(errno));
        close(server_sockfd);
        server_sockfd = -1;
        return -1;
    }

    /* Start listening for incoming connections */
    if (listen(server_sockfd, BACKLOG) < 0) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        close(server_sockfd);
        server_sockfd = -1;
        return -1;
    }

    syslog(LOG_INFO, "Server started on port %s\n", port);
    return 0; /* success */
}
#elif FLEXIBLE_SERVER_START
void print_addrinfo_node(struct addrinfo *node) {
    if (node == NULL) {
        printf("addrinfo node is NULL\n");
        return;
    }

    char ipstr[INET6_ADDRSTRLEN]; // Buffer para armazenar o IP como string
    void *addr;
    char *ipver;

    // Verifica o tipo de endereço
    if (node->ai_family == AF_INET) { // IPv4
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)node->ai_addr;
        addr = &(ipv4->sin_addr);
        ipver = "IPv4";
    } else if (node->ai_family == AF_INET6) { // IPv6
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)node->ai_addr;
        addr = &(ipv6->sin6_addr);
        ipver = "IPv6";
    } else {
        printf("Unknown address family: %d\n", node->ai_family);
        return;
    }

    // Converte o endereço IP para string
    inet_ntop(node->ai_family, addr, ipstr, sizeof(ipstr));

    // Imprime as informações
    printf("Address Info -> Family: %s, Address: %s, Socktype: %d, Protocol: %d, CanonName: %s\n", 
    ipver, ipstr, node->ai_socktype, node->ai_protocol, node->ai_canonname != NULL ? node->ai_canonname : "--");
}
/*
Baseado no "Bej's Guide to Network Programming", usa a função getaddrinfo(), que permite:
-Suporte a IPv4 e IPv6 (dependendo de como você configura hints.ai_family).
-Resolução flexível de endereço (usando nomes de host ou interfaces locais).
-Possibilidade de iterar por todas as combinações de endereços que o sistema retornar, até encontrar uma que consiga fazer bind com sucesso.

Esses passos tornam o seu servidor mais geral e mais robusto, pois:
-Ele pode usar IPv6 se disponível (caso AF_UNSPEC retorne endereços IPv6 primeiro ou se você mudar para AF_INET6).
-Ele não fica limitado a apenas um endereço (por exemplo, se a máquina tiver várias interfaces, ele pode tentar cada uma).
*/
int server_start(char* port) {
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int yes=1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    /* 
        getaddrinfo() is an excellent function that will return information on a particular host name (such as its IP address) 
        and load up a struct sockaddr for you, taking care of the gritty details (like if it’s IPv4 or IPv6). 
    */
    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s\n", gai_strerror(rv));
        return -1;
    }
    
      // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        print_addrinfo_node(p);

        /*
            socket() returns a new socket descriptor that you can use to do sockety things with. 
            This is generally the first call in the whopping process of writing a socket program, 
            and you can use the result for subsequent calls to listen(), bind(), accept(), or a variety of other functions.
        */
        if ((server_sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            syslog(LOG_ERR, "socket creation failed: %s", strerror(errno));
            continue;
        }
        
        // confifure the created socket
        if ( setsockopt(
                server_sockfd,     // the socket we want to configure 
                SOL_SOCKET, 
                SO_REUSEADDR | SO_REUSEPORT,    // int optname, SO_REUSEADDR allows other sockets to bind() to this port, unless there is an active listening socket bound to the port already. 
                                                // This enables you to get around those “Address already in use” error messages when you try to restart your server after a crash.
                &yes,         // void *optval, it’s usually a pointer to an int indicating the value in question. 
                              // For booleans, zero is false, and non-zero is true. And that’s an absolute fact, unless it’s different on your system. 
                              // If there is no parameter to be passed, optval can be NULL.
                sizeof(int)) == -1 //socklen_t optlen, should be set to the length of optval, probably sizeof(int), but varies depending on the option
        ) {
            syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
            return -1;
        }

        /* 
            bind() associate a socket with an IP address and port number.
            When a remote machine wants to connect to your server program, it needs two pieces of information: 
            the IP address and the port number. The bind() call allows you to do just that.
        */
        if (bind(server_sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(server_sockfd);
            syslog(LOG_ERR, "bind failed: %s", strerror(errno));
            continue;
        }

        break;
        
    }

    freeaddrinfo(servinfo); // all done with this structure

    if (p == NULL)  {
        syslog(LOG_ERR, "server: failed to bind");
        return -1;
    }

    if (listen(server_sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen: %s", strerror(errno));
        return -1;
    }

    syslog(LOG_INFO, "Server started on port %s\n", port);
    return 0; //sucess
}
#endif

/*
 * server_run: main loop that accepts new connections and spawns a thread 
 * for each client. 
 * 
 * It runs until keep_running is set to 0 (e.g., by a signal).
 */
 void server_run(void) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (keep_running) {
        int client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sockfd < 0) {
            /* If accept() was interrupted by a signal (keep_running is 0), we exit the loop */
            if (keep_running == 0) {
                break;
            }
            syslog(LOG_ERR, "accept: %s", strerror(errno));
            continue;
        }
        /* Allocate thread arguments for the new connection */
        ThreadArgs *args = (ThreadArgs *)malloc(sizeof(ThreadArgs));
        if (!args) {
            syslog(LOG_ERR, "ThreadArgs malloc: %s", strerror(errno));
            close(client_sockfd);
            continue;
        }
        // fill args
        args->client_sockfd = client_sockfd;
        // fill 'ip_str' with string version of client IP
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        strncpy(args->ip_str, ip_str, sizeof(ip_str));
        
        /* Create a thread to handle this connection */
        pthread_t tid;
        if (pthread_create(&tid, NULL, connection_handler, (void *)args) != 0) {
            syslog(LOG_ERR, "pthread_create: %s", strerror(errno));
            close(client_sockfd);
            free(args);
            continue;
        }

        /* Add the new thread to the global thread list */
        add_thread_to_list(tid);

        /* print client info */
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);
    }
}

/*
 * server_stop: closes the listening socket, waits for all active threads, 
 * and destroys the file_mutex.
 */
 int server_stop(void) {
    syslog(LOG_INFO, "Server is stopping...");
    
    keep_running = 0; // Clear flag for other threads to stop running

    /* Wait for all connection threads to complete */
    join_all_threads();

    /* If the listening socket is still open, close it */
    if (server_sockfd >= 0) {
        close(server_sockfd);
        server_sockfd = -1;
    }
    
    // Wait for timer_thread to finish.
    //pthread_join(timer_thread, NULL);

    /* Destroy the mutexes */
    pthread_mutex_destroy(&file_mutex);
    pthread_mutex_destroy(&thread_list_mutex);

    // Delete data file
    remove(DATA_FILE_PATH);  
    
    closelog();

    return 0; /* success */
}


