/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Atomic SPSC double buffer.
 *
 * One producer publishes frames, one consumer always reads the latest
 * complete one.  Lock-free: a single atomic byte.  T must be memcpy-able
 * (no owned pointers, or managed outside).
 *
 *   struct dbuf<Frame> b; dbuf_init<Frame>(&b, &first);
 *   // producer:             // consumer:
 *   dbuf_write<Frame>(&b,&f); dbuf_read<Frame>(&b, &out);
 */
#ifndef CZET_UTILS_DOUBLE_BUFFERING_H
#define CZET_UTILS_DOUBLE_BUFFERING_H

#include <stdatomic.h>
#include <string.h>

struct dbuf<T>
{
    T slots[2];
    atomic_uchar front;
};

void dbuf_init<T>(struct dbuf<T> *b, T *init)
{
    if (!b || !init)
        return;
    memcpy(&b->slots[0], init, sizeof(*init));
    memcpy(&b->slots[1], init, sizeof(*init));
    atomic_init(&b->front, (unsigned char)0);
}

/* Publish a copy of *v as the latest frame. */
void dbuf_write<T>(struct dbuf<T> *b, T *v)
{
    unsigned char f;

    if (!b || !v)
        return;
    f = atomic_load_explicit(&b->front, memory_order_relaxed);
    memcpy(&b->slots[f ^ 1], v, sizeof(*v));
    atomic_store_explicit(&b->front, (unsigned char)(f ^ 1),
                          memory_order_release);
}

/* Copy the latest published frame into *out. */
void dbuf_read<T>(struct dbuf<T> *b, T *out)
{
    unsigned char f;

    if (!b || !out)
        return;
    f = atomic_load_explicit(&b->front, memory_order_acquire);
    memcpy(out, &b->slots[f], sizeof(*out));
}

#endif /* CZET_UTILS_DOUBLE_BUFFERING_H */
