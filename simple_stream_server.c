/*
* simple_stream_server.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>


#define PORT "9000" // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#define TMP_DATA_FILE "/var/tmp/SimpleServerSocketData"
#define BUFFER_SIZE 1024

int server_sockfd = -1; // server socket file descriptor
int client_sockfd = -1; // client socket file descriptor

void handle_signal(int sig) {
    (void)sig; // quiet unused variable warning

    syslog(LOG_INFO, "Caught signal, exiting");

    //Close any open socket
    if (server_sockfd != -1) close(server_sockfd);
    if (client_sockfd != -1) close(client_sockfd);

    // Delete data file
    remove(TMP_DATA_FILE);  
    closelog();
    exit(0);
}

void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

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

int open_server_socket() {
    struct addrinfo hints, *servinfo, *p;
    int rv;
    int yes=1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    
    /* 
        getaddrinfo() is an excellent function that will return information on a particular host name (such as its IP address) 
        and load up a struct sockaddr for you, taking care of the gritty details (like if it’s IPv4 or IPv6). 
    */
    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
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
                SO_REUSEADDR, // int optname, SO_REUSEADDR allows other sockets to bind() to this port, unless there is an active listening socket bound to the port already. 
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

    return 1;
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

int wait_connection_from_client(char* client_ip, size_t ip_len) {
    struct sockaddr_storage client_addr;
    socklen_t addr_size = sizeof(client_addr);
    client_sockfd = accept(server_sockfd, (struct sockaddr *)&client_addr, &addr_size);
    if (client_sockfd == -1) {
        syslog(LOG_ERR, "accept failed: %s", strerror(errno));
        return -1;
    }

    // Captures client's IP and register on syslog
    inet_ntop(client_addr.ss_family, 
                (client_addr.ss_family == AF_INET)
                ? (void *)&(((struct sockaddr_in *)&client_addr)->sin_addr)
                : (void *)&(((struct sockaddr_in6 *)&client_addr)->sin6_addr),
                client_ip, ip_len);
    
    return 0;
}

// Receives dada from client and appends to TMP_DATA_FILE, creating this file if it doesn’t exist.
int recv_client_data_and_append_to_file(char *data_buf, size_t buf_len) {
    int data_file = open(TMP_DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (data_file == -1) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        close(client_sockfd);
        return -1;
    }

    // We dont receive the client data at once because it can be very large, 
    // therefore we only received a total of 'buf_len' bytes, and append it.
    // Repeat the processes in a loop until all the data has been received.
    ssize_t bytes_received;
    while ((bytes_received = recv(client_sockfd, data_buf, buf_len, 0)) > 0) {
        write(data_file, data_buf, bytes_received);
        // If found '\n', stop reading
        if (memchr(data_buf, '\n', bytes_received)) break;
    }
    close(data_file);

    return 0;
}

// Returns the full content of TMP_DATA_FILE to the client as soon as the received data packet completes.
int send_file_data_to_client(char *data_buf, size_t buf_len) {
    int data_file = open(TMP_DATA_FILE, O_RDONLY);
    if (data_file == -1) {
        syslog(LOG_ERR, "Failed to open data file for reading");
        close(client_sockfd);
        return -1;
    }

    // We dont read the file at once because it can be very large, 
    // therefore we only read a total of 'buf_len' bytes, and send it.
    // Repeat the processes in a loop until all the data has been send.
    ssize_t bytes_received;
    while ((bytes_received = read(data_file, data_buf, buf_len)) > 0) {
        send(client_sockfd, data_buf, bytes_received, 0);
    }
    close(data_file);
    return 0;
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;

    // Verify args to detect deamon mode (-d)
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("SimpleStreamServer", LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);
    setup_signal_handlers();

    if(!open_server_socket()) {
        return -1;
    }

    syslog(LOG_INFO, "Server listening on port %s", PORT);

    if (daemon_mode) {
        syslog(LOG_INFO, "Running in daemon mode");
        daemonize();
    }

    // Wait for a connection from a client, 
    // receives data from it and append to file, 
    // returns the full file data back to the client, 
    // then closes the connection
    char client_ip[INET6_ADDRSTRLEN];
    char buffer[BUFFER_SIZE];
    while (1) {
        if(wait_connection_from_client(client_ip, sizeof(client_ip)) == -1) {
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        if(recv_client_data_and_append_to_file(buffer, BUFFER_SIZE) == -1) {
            continue;
        }

        if(send_file_data_to_client(buffer, BUFFER_SIZE) == -1) {
            continue;
        }

        syslog(LOG_INFO, "Closed connection from %s", client_ip);
        close(client_sockfd);
    }
}
