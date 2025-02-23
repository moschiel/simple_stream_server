#ifndef SERVER_H
#define SERVER_H

#define PROCESS_NAME "simple_stream_server"
#define DATA_FILE_PATH "/var/tmp/" PROCESS_NAME "data"

#define USE_SYSCALL_FILE 1
#define USE_STDIO_FILE (!USE_SYSCALL_FILE)

#if USE_SYSCALL_FILE
#define INVALID_FILE -1
#else
#define INVALID_FILE NULL
#endif

#endif