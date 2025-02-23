#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

/*
 * ThreadArgs:
 * A structure to hold the arguments needed by the connection handler thread.
 * In this example, we only store the client socket descriptor.
 */
typedef struct ThreadArgs {
    int client_sockfd; // client socket file descriptor
    char ip_str[INET_ADDRSTRLEN];
} ThreadArgs;

/*
 * connection_handler:
 * The function that each new thread runs to handle a client connection.
 * Receives the socket descriptor in ThreadArgs, then reads data from the client
 * and writes it to file specified at DATA_FILE_PATH (protected by a mutex).
 */
 void *connection_handler(void *args);

#endif /* CONNECTION_HANDLER_H */