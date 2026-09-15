/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Cooperative coroutines, thin wrapper over libaco.
 *
 * Upstream libaco (hnes/libaco, Apache-2.0) ships prebuilt in CZet:
 * its .c/.S are already linked, just
 *   #include <utils/libaco.h>
 * This header adds a minimal round-robin scheduler:
 *
 *   struct co_sched s; co_sched_init(&s);
 *   co_spawn(&s, work, &arg, 0);   // stack 0 = 1 MiB default
 *   co_spawn(&s, more, &arg2, 0);
 *   co_run(&s);                    // until every task is done
 *   co_sched_free(&s);
 *
 * Inside a task, co_yield() returns to the scheduler.  Each task owns
 * its stack (save_stack_sz = 0).  A task must NEVER plain-return: the
 * trampoline marks it done and yields; co_run never resumes it.
 * One scheduler per thread (aco_thread_init is per-thread).
 */
#ifndef CZET_UTILS_ACO_H
#define CZET_UTILS_ACO_H

#include <stdlib.h>
#include <utils/libaco.h>

struct co_task;
struct co_sched;

struct co_task
{
    aco_t *co;
    aco_share_stack_t *stack;
    void (*fn)(void *arg);
    void *arg;
    int done;
    struct co_sched *sched;
};

struct co_sched
{
    aco_t *main_co;
    struct co_task **tasks;
    unsigned long count;
    unsigned long cap;
    struct co_task *current;
};

static void co_task_entry(void)
{
    struct co_task *t = (struct co_task *)aco_get_arg();
    t->fn(t->arg);
    t->done = 1;
    aco_yield();
    /* Unreachable: co_run never resumes a finished task. */
    for (;;)
        aco_yield();
}

static int co_sched_init(struct co_sched *s)
{
    if (!s)
        return -1;
    aco_thread_init((void *)0);
    s->main_co = aco_create((aco_t *)0, (aco_share_stack_t *)0,
                            (size_t)0, (aco_cofuncp_t)0, (void *)0);
    if (!s->main_co)
        return -1;
    s->tasks = (struct co_task **)0;
    s->count = 0;
    s->cap = 0;
    s->current = (struct co_task *)0;
    return 0;
}

static struct co_task *co_spawn(struct co_sched *s, void (*fn)(void *arg),
                                void *arg, size_t stack_size)
{
    struct co_task *t;

    if (!s || !s->main_co || !fn)
        return (struct co_task *)0;
    if (s->count == s->cap)
    {
        unsigned long ncap = s->cap ? s->cap * 2 : 8;
        struct co_task **nt = (struct co_task **)realloc(
            s->tasks, ncap * sizeof(*nt));
        if (!nt)
            return (struct co_task *)0;
        s->tasks = nt;
        s->cap = ncap;
    }
    t = (struct co_task *)calloc(1, sizeof(*t));
    if (!t)
        return (struct co_task *)0;
    t->stack = aco_share_stack_new(stack_size);
    if (!t->stack)
    {
        free(t);
        return (struct co_task *)0;
    }
    t->fn = fn;
    t->arg = arg;
    t->done = 0;
    t->sched = s;
    t->co = aco_create(s->main_co, t->stack, (size_t)0,
                       co_task_entry, (void *)t);
    if (!t->co)
    {
        aco_share_stack_destroy(t->stack);
        free(t);
        return (struct co_task *)0;
    }
    s->tasks[s->count++] = t;
    return t;
}

/* Yield from inside a task back to the scheduler. */
static void co_yield(void)
{
    aco_yield();
}

/* Run round-robin until every task is done. */
static void co_run(struct co_sched *s)
{
    unsigned long i;
    int pending;

    if (!s)
        return;
    do
    {
        pending = 0;
        for (i = 0; i < s->count; i++)
        {
            if (s->tasks[i]->done)
                continue;
            pending = 1;
            s->current = s->tasks[i];
            aco_resume(s->tasks[i]->co);
        }
    } while (pending);
    s->current = (struct co_task *)0;
}

static void co_task_free(struct co_task *t)
{
    if (!t)
        return;
    if (t->co)
        aco_destroy(t->co);
    if (t->stack)
        aco_share_stack_destroy(t->stack);
    free(t);
}

static void co_sched_free(struct co_sched *s)
{
    unsigned long i;

    if (!s)
        return;
    for (i = 0; i < s->count; i++)
        co_task_free(s->tasks[i]);
    free(s->tasks);
    if (s->main_co)
        aco_destroy(s->main_co);
    s->tasks = (struct co_task **)0;
    s->count = 0;
    s->cap = 0;
    s->main_co = (aco_t *)0;
}

#endif /* CZET_UTILS_ACO_H */
