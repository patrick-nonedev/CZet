/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Atomic SPSC triple buffer.
 *
 * Like the double buffer, but the producer never steps on the slot being
 * read nor drops the newest datum when the reader lags: a third spare
 * slot absorbs it.  Lock-free: a single atomic byte.  SPSC, memcpy-able T,
 * same contract as <utils/double_buffering.h>.
 *
 * State packed in one byte: front[0:2] | shared[2:4] | dirty[4].
 */
#ifndef CZET_UTILS_TRIPLE_BUFFERING_H
#define CZET_UTILS_TRIPLE_BUFFERING_H

#include <stdatomic.h>
#include <string.h>

struct tbuf<T>
{
    T slots[3];
    atomic_uchar state;
};

void tbuf_init<T>(struct tbuf<T> *b, T *init)
{
    if (!b || !init)
        return;
    memcpy(&b->slots[0], init, sizeof(*init));
    memcpy(&b->slots[1], init, sizeof(*init));
    memcpy(&b->slots[2], init, sizeof(*init));
    /* front=0, shared=0, dirty=0 */
    atomic_init(&b->state, (unsigned char)0);
}

void tbuf_write<T>(struct tbuf<T> *b, T *v)
{
    unsigned char s, front, shared, back;

    if (!b || !v)
        return;
    s = atomic_load_explicit(&b->state, memory_order_relaxed);
    front = (unsigned char)(s & 3);
    shared = (unsigned char)((s >> 2) & 3);
    if (front == shared)
        back = (unsigned char)((front + 1) % 3);
    else
        back = (unsigned char)(3 - front - shared);
    memcpy(&b->slots[back], v, sizeof(*v));
    s = (unsigned char)(front | (back << 2) | (1 << 4));
    atomic_store_explicit(&b->state, s, memory_order_release);
}

void tbuf_read<T>(struct tbuf<T> *b, T *out)
{
    unsigned char s, ns, front;

    if (!b || !out)
        return;
    /* On fresh data (dirty), front becomes shared.  CAS in case the
     * producer publishes between our load and store. */
    for (;;)
    {
        s = atomic_load_explicit(&b->state, memory_order_acquire);
        if (!(s & (1 << 4)))
        {
            front = (unsigned char)(s & 3);
            break;
        }
        ns = (unsigned char)(((s >> 2) & 3) | (s & ~((unsigned char)0x13)));
        if (atomic_compare_exchange_weak_explicit(
                &b->state, &s, ns, memory_order_acq_rel,
                memory_order_acquire))
        {
            front = (unsigned char)(ns & 3);
            break;
        }
    }
    memcpy(out, &b->slots[front], sizeof(*out));
}

#endif /* CZET_UTILS_TRIPLE_BUFFERING_H */
