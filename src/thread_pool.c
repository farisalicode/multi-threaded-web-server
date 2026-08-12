#include "thread_pool.h"

#include <errno.h>
#include <stdlib.h>

static void *worker_main(void *arg) {
    thread_pool_t *pool = (thread_pool_t *)arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->queue_size == 0 && !pool->shutting_down) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        if (pool->queue_size == 0 && pool->shutting_down) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        job_t *job = pool->head;
        pool->head = job->next;
        pool->queue_size--;

        if (pool->queue_size == 0) {
            pool->tail = NULL;
        }

        pthread_cond_signal(&pool->not_full);
        pthread_mutex_unlock(&pool->mutex);

        job->function(job->arg);
        free(job);
    }

    return NULL;
}

int thread_pool_init(thread_pool_t *pool, int thread_count, int max_queue_size) {
    if (!pool || thread_count <= 0 || max_queue_size <= 0) {
        return -1;
    }

    pool->threads = calloc((size_t)thread_count, sizeof(*pool->threads));
    if (!pool->threads) {
        return -1;
    }

    pool->thread_count = thread_count;
    pool->head = NULL;
    pool->tail = NULL;
    pool->queue_size = 0;
    pool->max_queue_size = max_queue_size;
    pool->shutting_down = 0;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0 ||
        pthread_cond_init(&pool->not_empty, NULL) != 0 ||
        pthread_cond_init(&pool->not_full, NULL) != 0) {
        free(pool->threads);
        pool->threads = NULL;
        return -1;
    }

    for (int i = 0; i < thread_count; ++i) {
        if (pthread_create(&pool->threads[i], NULL, worker_main, pool) != 0) {
            pthread_mutex_lock(&pool->mutex);
            pool->shutting_down = 1;
            pthread_cond_broadcast(&pool->not_empty);
            pthread_mutex_unlock(&pool->mutex);

            for (int j = 0; j < i; ++j) {
                pthread_join(pool->threads[j], NULL);
            }

            pthread_cond_destroy(&pool->not_empty);
            pthread_cond_destroy(&pool->not_full);
            pthread_mutex_destroy(&pool->mutex);
            free(pool->threads);
            pool->threads = NULL;
            return -1;
        }
    }

    return 0;
}

int thread_pool_submit(thread_pool_t *pool, job_fn function, void *arg) {
    if (!pool || !function) {
        return -1;
    }

    job_t *job = malloc(sizeof(*job));
    if (!job) {
        return -1;
    }

    job->function = function;
    job->arg = arg;
    job->next = NULL;

    pthread_mutex_lock(&pool->mutex);

    while (pool->queue_size >= pool->max_queue_size && !pool->shutting_down) {
        pthread_cond_wait(&pool->not_full, &pool->mutex);
    }

    if (pool->shutting_down) {
        pthread_mutex_unlock(&pool->mutex);
        free(job);
        return -1;
    }

    if (pool->tail) {
        pool->tail->next = job;
    } else {
        pool->head = job;
    }
    pool->tail = job;
    pool->queue_size++;

    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void thread_pool_shutdown(thread_pool_t *pool) {
    if (!pool) {
        return;
    }

    pthread_mutex_lock(&pool->mutex);
    pool->shutting_down = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_cond_broadcast(&pool->not_full);
    pthread_mutex_unlock(&pool->mutex);

    for (int i = 0; i < pool->thread_count; ++i) {
        pthread_join(pool->threads[i], NULL);
    }
}

void thread_pool_destroy(thread_pool_t *pool) {
    if (!pool) {
        return;
    }

    job_t *job = pool->head;
    while (job) {
        job_t *next = job->next;
        free(job);
        job = next;
    }

    pthread_cond_destroy(&pool->not_empty);
    pthread_cond_destroy(&pool->not_full);
    pthread_mutex_destroy(&pool->mutex);

    free(pool->threads);
    pool->threads = NULL;
}
