/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Linear (bump) allocator with reset.
 *
 * Everything is freed together by arena_free() or recycled by
 * arena_reset().  No per-object free: for scratch memory and parsing.
 * Header-only `static`, no extra flags.
 */
#ifndef CZET_UTILS_ARENA_H
#define CZET_UTILS_ARENA_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct arena
{
    unsigned char *base;
    unsigned char *ptr;
    unsigned char *end;
};

/* Reserve cap bytes (0 allows an empty arena).  0 ok, -1 error. */
static int arena_init(struct arena *a, unsigned long cap)
{
    if (!a)
        return -1;
    a->base = (unsigned char *)0;
    a->ptr = (unsigned char *)0;
    a->end = (unsigned char *)0;
    if (cap == 0)
        return 0;
    a->base = (unsigned char *)malloc(cap);
    if (!a->base)
        return -1;
    a->ptr = a->base;
    a->end = a->base + cap;
    return 0;
}

/* Reserve size bytes aligned to align (0 = natural alignment).
 * Return the pointer, or NULL when it does not fit.  Never fails on
 * system OOM: capacity was fixed by arena_init. */
static void *arena_alloc(struct arena *a, unsigned long size,
                         unsigned long align)
{
    uintptr_t cur, aligned;

    if (!a || !a->base)
        return (void *)0;
    if (align == 0)
        align = _Alignof(max_align_t);
    /* align must be a power of two */
    if ((align & (align - 1)) != 0)
        return (void *)0;
    cur = (uintptr_t)a->ptr;
    aligned = (cur + (uintptr_t)(align - 1)) & ~(uintptr_t)(align - 1);
    if (size > (unsigned long)(a->end - (unsigned char *)aligned))
        return (void *)0;
    a->ptr = (unsigned char *)aligned + size;
    return (void *)aligned;
}

/* Like arena_alloc, zeroed. */
static void *arena_calloc(struct arena *a, unsigned long count,
                          unsigned long size, unsigned long align)
{
    void *p;

    if (size != 0 && count > (~(unsigned long)0) / size)
        return (void *)0;
    p = arena_alloc(a, count * size, align);
    if (p)
        memset(p, 0, count * size);
    return p;
}

static unsigned long arena_used(const struct arena *a)
{
    if (!a || !a->base)
        return 0;
    return (unsigned long)(a->ptr - a->base);
}

static unsigned long arena_capacity(const struct arena *a)
{
    if (!a || !a->base)
        return 0;
    return (unsigned long)(a->end - a->base);
}

/* Recycle the whole arena without returning memory. */
static void arena_reset(struct arena *a)
{
    if (a)
        a->ptr = a->base;
}

static void arena_free(struct arena *a)
{
    if (!a)
        return;
    free(a->base);
    a->base = (unsigned char *)0;
    a->ptr = (unsigned char *)0;
    a->end = (unsigned char *)0;
}

#endif /* CZET_UTILS_ARENA_H */
