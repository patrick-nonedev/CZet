/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Open-addressed string -> void* map (linear probing).
 *
 * Keys are stored BY POINTER (never duplicated): they must outlive the
 * map (e.g. literals or arena memory).  NULL values read as "missing":
 * don't store NULLs.  Header-only `static`, no extra flags.
 */
#ifndef CZET_UTILS_HASHMAP_H
#define CZET_UTILS_HASHMAP_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HMAP_EMPTY 0
#define HMAP_USED 1
#define HMAP_TOMB 2

struct hmap_entry
{
    const char *key;
    void *val;
    unsigned char state;
};

struct hmap
{
    struct hmap_entry *tab;
    unsigned long cap;   /* always a power of two */
    unsigned long count; /* live entries */
    unsigned long tombs; /* tombstones */
};

static unsigned long hmap_hash(const char *s)
{
    /* FNV-1a 64 */
    unsigned long h = 1469598103934665603UL;
    while (*s)
    {
        h ^= (unsigned long)(unsigned char)*s++;
        h *= 1099511628211UL;
    }
    return h;
}

static int hmap_init(struct hmap *m)
{
    if (!m)
        return -1;
    m->cap = 16;
    m->count = 0;
    m->tombs = 0;
    m->tab = (struct hmap_entry *)calloc(m->cap, sizeof(*m->tab));
    return m->tab ? 0 : -1;
}

static void hmap_free(struct hmap *m)
{
    if (!m)
        return;
    free(m->tab);
    m->tab = (struct hmap_entry *)0;
    m->cap = 0;
    m->count = 0;
    m->tombs = 0;
}

static unsigned long hmap_count(const struct hmap *m)
{
    return m ? m->count : 0;
}

/* Probe the slot for key: *found=1 when present. */
static unsigned long hmap_probe(const struct hmap *m, const char *key,
                                int *found)
{
    unsigned long mask = m->cap - 1;
    unsigned long i = hmap_hash(key) & mask;
    unsigned long first_tomb = m->cap; /* sentinel: no tombstone seen */

    for (;;)
    {
        unsigned char st = m->tab[i].state;
        if (st == HMAP_EMPTY)
        {
            *found = 0;
            return (first_tomb != m->cap) ? first_tomb : i;
        }
        if (st == HMAP_TOMB)
        {
            if (first_tomb == m->cap)
                first_tomb = i;
        }
        else if (strcmp(m->tab[i].key, key) == 0)
        {
            *found = 1;
            return i;
        }
        i = (i + 1) & mask;
    }
}

static int hmap_rehash(struct hmap *m, unsigned long ncap);

static int hmap_put(struct hmap *m, const char *key, void *val)
{
    int found;
    unsigned long i;

    if (!m || !key || !val)
        return -1;
    if ((m->count + m->tombs + 1) * 10 >= m->cap * 7
        && hmap_rehash(m, m->cap * 2) != 0)
        return -1;
    i = hmap_probe(m, key, &found);
    if (!found)
    {
        if (m->tab[i].state == HMAP_TOMB)
            m->tombs--;
        m->tab[i].state = HMAP_USED;
        m->tab[i].key = key;
        m->count++;
    }
    m->tab[i].val = val;
    return 0;
}

static void *hmap_get(const struct hmap *m, const char *key)
{
    int found;
    unsigned long i;

    if (!m || !key || !m->tab)
        return (void *)0;
    i = hmap_probe(m, key, &found);
    return found ? m->tab[i].val : (void *)0;
}

/* 0 when deleted, -1 when missing. */
static int hmap_del(struct hmap *m, const char *key)
{
    int found;
    unsigned long i;

    if (!m || !key || !m->tab)
        return -1;
    i = hmap_probe(m, key, &found);
    if (!found)
        return -1;
    m->tab[i].state = HMAP_TOMB;
    m->tab[i].val = (void *)0;
    m->count--;
    m->tombs++;
    return 0;
}

static int hmap_rehash(struct hmap *m, unsigned long ncap)
{
    struct hmap_entry *nt;
    unsigned long i;

    nt = (struct hmap_entry *)calloc(ncap, sizeof(*nt));
    if (!nt)
        return -1;
    {
        struct hmap_entry *ot = m->tab;
        unsigned long ocap = m->cap;
        m->tab = nt;
        m->cap = ncap;
        m->count = 0;
        m->tombs = 0;
        for (i = 0; i < ocap; i++)
            if (ot[i].state == HMAP_USED)
                hmap_put(m, ot[i].key, ot[i].val);
        free(ot);
    }
    return 0;
}

#endif /* CZET_UTILS_HASHMAP_H */
