/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Minimal pthread thread pool (bounded queue, pool_wait, try_submit).
 * Header-only `static`.  Link with -pthread explicitly.
 */
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Task signature returns void* (return value is ignored). */
typedef void *(*pool_task_fn)(void *arg);

typedef struct pool_task
{
    pool_task_fn fn;
    void *arg;
} pool_task_t;

typedef struct pool
{
    pthread_t *threads;
    size_t num_threads;

    pool_task_t *queue;
    size_t queue_size;

    size_t head;
    size_t tail;
    size_t count;

    /*
     * Tasks currently running; backs pool_wait().
     */
    size_t active;

    pthread_mutex_t mutex;

    pthread_cond_t cond_not_empty;

    pthread_cond_t cond_not_full;

    pthread_cond_t cond_finished;

    int shutdown;
} pool_t;


/* ============================================================
 * Internal
 * ============================================================ */

static void *pool_worker(void *arg)
{
    pool_t *pool = (pool_t *)arg;

    for (;;)
    {
        pool_task_t task;

        pthread_mutex_lock(&pool->mutex);

        /* Wait while idle with the pool still alive. */
        while (pool->count == 0 && !pool->shutdown)
        {
            pthread_cond_wait(
                &pool->cond_not_empty,
                &pool->mutex
            );
        }

        /* Shutting down with an empty queue: this worker exits. */
        if (pool->count == 0 && pool->shutdown)
        {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Pop one task. */
        task = pool->queue[pool->head];

        pool->head = (pool->head + 1) % pool->queue_size;
        pool->count--;

        pool->active++;

        /* Queue has room again. */
        pthread_cond_signal(&pool->cond_not_full);

        pthread_mutex_unlock(&pool->mutex);

        /* Run without holding the mutex; drop the return value. */
        (void)task.fn(task.arg);

        pthread_mutex_lock(&pool->mutex);

        pool->active--;

        /* Queue drained and nobody running: wake pool_wait(). */
        if (pool->count == 0 && pool->active == 0)
        {
            pthread_cond_broadcast(&pool->cond_finished);
        }

        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}


/* ============================================================
 * API
 * ============================================================ */

static pool_t *pool_create(size_t num_threads, size_t queue_size)
{
    pool_t *pool = NULL;
    size_t i;

    if (num_threads == 0 || queue_size == 0)
    {
        return NULL;
    }

    pool = (pool_t *)calloc(1, sizeof(*pool));
    if (!pool)
    {
        return NULL;
    }

    pool->threads = (pthread_t *)calloc(num_threads, sizeof(*pool->threads));
    if (!pool->threads)
    {
        free(pool);
        return NULL;
    }

    pool->queue = (pool_task_t *)calloc(queue_size, sizeof(*pool->queue));
    if (!pool->queue)
    {
        free(pool->threads);
        free(pool);
        return NULL;
    }

    pool->num_threads = num_threads;
    pool->queue_size = queue_size;

    if (pthread_mutex_init(&pool->mutex, NULL) != 0)
    {
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_not_empty, NULL) != 0)
    {
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_not_full, NULL) != 0)
    {
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_finished, NULL) != 0)
    {
        pthread_cond_destroy(&pool->cond_not_full);
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool->queue);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    /* Spawn workers. */
    for (i = 0; i < num_threads; ++i)
    {
        if (pthread_create(
                    &pool->threads[i],
                    NULL,
                    pool_worker,
                    pool
                ) != 0)
        {
            pthread_mutex_lock(&pool->mutex);
            pool->shutdown = 1;
            pthread_cond_broadcast(&pool->cond_not_empty);
            pthread_cond_broadcast(&pool->cond_not_full);
            pthread_mutex_unlock(&pool->mutex);

            for (size_t j = 0; j < i; ++j)
            {
                pthread_join(pool->threads[j], NULL);
            }

            pthread_cond_destroy(&pool->cond_finished);
            pthread_cond_destroy(&pool->cond_not_full);
            pthread_cond_destroy(&pool->cond_not_empty);
            pthread_mutex_destroy(&pool->mutex);

            free(pool->queue);
            free(pool->threads);
            free(pool);

            return NULL;
        }
    }

    return pool;
}


static int pool_submit(
    pool_t *pool,
    pool_task_fn fn,
    void *arg
)
{
    if (!pool || !fn)
    {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);

    while (pool->count == pool->queue_size && !pool->shutdown)
    {
        pthread_cond_wait(
            &pool->cond_not_full,
            &pool->mutex
        );
    }

    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pool->queue[pool->tail].fn = fn;
    pool->queue[pool->tail].arg = arg;

    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;

    pthread_cond_signal(&pool->cond_not_empty);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}


static int pool_try_submit(
    pool_t *pool,
    pool_task_fn fn,
    void *arg
)
{
    if (!pool || !fn)
    {
        return -1;
    }

    pthread_mutex_lock(&pool->mutex);

    if (pool->shutdown || pool->count == pool->queue_size)
    {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    pool->queue[pool->tail].fn = fn;
    pool->queue[pool->tail].arg = arg;

    pool->tail = (pool->tail + 1) % pool->queue_size;
    pool->count++;

    pthread_cond_signal(&pool->cond_not_empty);

    pthread_mutex_unlock(&pool->mutex);

    return 0;
}


static void pool_wait(pool_t *pool)
{
    if (!pool)
    {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    while (pool->count != 0 || pool->active != 0)
    {
        pthread_cond_wait(
            &pool->cond_finished,
            &pool->mutex
        );
    }

    pthread_mutex_unlock(&pool->mutex);
}


static void pool_destroy(pool_t *pool)
{
    size_t i;

    if (!pool)
    {
        return;
    }

    pthread_mutex_lock(&pool->mutex);

    pool->shutdown = 1;

    pthread_cond_broadcast(&pool->cond_not_empty);
    pthread_cond_broadcast(&pool->cond_not_full);

    pthread_mutex_unlock(&pool->mutex);

    for (i = 0; i < pool->num_threads; ++i)
    {
        pthread_join(pool->threads[i], NULL);
    }

    pthread_cond_destroy(&pool->cond_finished);
    pthread_cond_destroy(&pool->cond_not_full);
    pthread_cond_destroy(&pool->cond_not_empty);
    pthread_mutex_destroy(&pool->mutex);

    free(pool->queue);
    free(pool->threads);
    free(pool);
}

#ifdef __cplusplus
}
#endif

#endif /* THREAD_POOL_H */
