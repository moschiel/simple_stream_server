#include "thread_list.h"

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>


/*
 * We maintain a global head pointer (thread_list_head)
 * to the singly linked list of threads, as well as a mutex
 * to protect modifications to this list.
 */
static ThreadNode *thread_list_head = NULL;
pthread_mutex_t thread_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * add_thread_to_list:
 * Allocates a new ThreadNode to store the given pthread_t tid
 * and inserts it at the beginning of the list.
 */
 void add_thread_to_list(pthread_t tid) {
    ThreadNode *node = (ThreadNode *)malloc(sizeof(ThreadNode));
    if (!node) {
        syslog(LOG_ERR, "ThreadNode malloc: %s", strerror(errno));
        return;
    }
    node->thread_id = tid;
    node->exited = 0;
    node->next = NULL;

    pthread_mutex_lock(&thread_list_mutex);
    node->next = thread_list_head;
    thread_list_head = node;
    pthread_mutex_unlock(&thread_list_mutex);
}

/*
 * remove_thread_from_list:
 * Searches the list for the given pthread_t tid.
 * When found, removes it from the list and frees the node.
 */
void remove_thread_from_list(pthread_t tid) {
    pthread_mutex_lock(&thread_list_mutex);

    ThreadNode *prev = NULL;
    ThreadNode *curr = thread_list_head;

    while (curr != NULL) {
        if (pthread_equal(curr->thread_id, tid)) {
            /* Found the node */
            if (prev == NULL) {
                /* Removing the head node */
                thread_list_head = curr->next;
            } else {
                /* Removing a middle or tail node */
                prev->next = curr->next;
            }

            syslog(LOG_INFO, "free thread node (tid: %lu)", curr->thread_id);            
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&thread_list_mutex);
}

/*
 * set_thread_as_exited:
 * search the list for the given pthread_t tid.
 * when found set as 'exited'.
 */
 void set_thread_as_exited(pthread_t tid) {
    pthread_mutex_lock(&thread_list_mutex);

    ThreadNode *node = thread_list_head;

    while (node != NULL) {
        if (pthread_equal(node->thread_id, tid)) {
            /* Found the node */
            node->exited = 1;
            syslog(LOG_INFO, "set thread node as 'exited' (tid: %lu)", node->thread_id);            
            break;
        }
        node = node->next;
    }

    pthread_mutex_unlock(&thread_list_mutex);
}


/*
 * join_exited_threads:
 * search the list for any pthread_t marked as 'exited'.
 * when found, join the thread, and free its node from the thread list.
 */
 void join_exited_threads(void) {
    pthread_mutex_lock(&thread_list_mutex);

    ThreadNode *prev = NULL;
    ThreadNode *curr = thread_list_head;

    while (curr != NULL) {
        if (curr->exited) {
            /* Found exited node */
            syslog(LOG_INFO, "joining 'exited' thread (tid: %lu)", curr->thread_id);            
            pthread_join(curr->thread_id, NULL);

            if (prev == NULL) {
                /* Removing the head node */
                thread_list_head = curr->next;
            } else {
                /* Removing a middle or tail node */
                prev->next = curr->next;
            }

            syslog(LOG_INFO, "free thread node (tid: %lu)", curr->thread_id);            
            free(curr);
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    pthread_mutex_unlock(&thread_list_mutex);
}

/*
 * join_all_threads:
 * Iterates over the list of threads, performing pthread_join on each.
 * And remove the thread from the list as well,
 * we repeatedly check until the list is empty.
 */
 void join_all_threads(void) {
    syslog(LOG_INFO, "join_all_threads");
    while (1) {
        pthread_mutex_lock(&thread_list_mutex);
        
        ThreadNode *node = thread_list_head;
        if (!node) {
            syslog(LOG_INFO, "no threads to join");
            /* The list is empty; no more threads to join */
            pthread_mutex_unlock(&thread_list_mutex);
            break;
        }

        /* Take the first thread in the list */
        pthread_t tid = node->thread_id;
        pthread_mutex_unlock(&thread_list_mutex);

        /*
         * Wait for this thread to finish.
         * remove_thread_from_list() is typically called by the
         * thread itself upon exit. But if it's not removed yet,
         * we can also remove it after join to ensure no memory leaks.
         */
        pthread_join(tid, NULL);
        syslog(LOG_INFO,"pthread_join tid: %lu", tid);

        /*
         * Remove the thread from the list
         */
        remove_thread_from_list(tid);
    }
}