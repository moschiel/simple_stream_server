#ifndef thread_list_H
#define thread_list_H

#include <pthread.h>

/*
 * ThreadNode:
 * A node in the singly linked list that stores a pthread_t.
 */
typedef struct ThreadNode {
    pthread_t thread_id;
    int exited;
    struct ThreadNode *next;
} ThreadNode;


void add_thread_to_list(pthread_t tid);
void remove_thread_from_list(pthread_t tid);
void set_thread_as_exited(pthread_t tid);
void join_exited_threads(void);
void join_all_threads(void);

#endif