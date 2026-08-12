#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>

typedef void (*job_fn)(void *arg);

typedef struct job {
    job_fn function;
    void *arg;
    struct job *next;
} job_t;

typedef struct {
    pthread_t *threads;
    int thread_count;

    job_t *head;
    job_t *tail;
    int queue_size;

    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;

    int max_queue_size;
    int shutting_down;
} thread_pool_t;

int thread_pool_init(thread_pool_t *pool, int thread_count, int max_queue_size);
int thread_pool_submit(thread_pool_t *pool, job_fn function, void *arg);
void thread_pool_shutdown(thread_pool_t *pool);
void thread_pool_destroy(thread_pool_t *pool);

#endif
