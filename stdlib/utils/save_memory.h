/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Endian-aware binary save/load via _Reflect.
 *
 * On-disk format: little-endian, packed (NO padding), no pointers, no
 * versioning.  Reader and writer must share the layout (same fields in
 * the same order); memory padding is never written.
 *
 *   reflect_t r = _Reflect(*v);
 *   sm_save_r(f, v, &r);   // 0 ok, -1 error
 *   sm_load_r(f, v, &r);
 *
 * Generic shortcuts (one TU per instantiation, like all generics):
 *   sm_save<T>(f, v);  sm_load<T>(f, v);
 *
 * Supported: integers (incl. _Bool, _BitInt, enums by size), floats
 * (bitwise copy), arrays (element by element), structs (field by field,
 * recursive), unions (LARGEST field only) and typedefs (their underlying
 * type).  Pointers, functions and void return -1: pointers are meaningless
 * outside the process.
 */
#ifndef CZET_UTILS_SAVE_MEMORY_H
#define CZET_UTILS_SAVE_MEMORY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sm_is_little_endian(void)
{
    const unsigned x = 1;
    return *(const unsigned char *)&x == 1;
}

static void sm_swap_bytes(void *p, unsigned long n)
{
    unsigned char *a = (unsigned char *)p;
    unsigned long i, j;
    for (i = 0, j = n ? n - 1 : 0; i < j; i++, j--)
    {
        unsigned char t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static int sm_write_raw(FILE *f, const void *p, unsigned long n)
{
    if (n == 0)
        return 0;
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

static int sm_read_raw(FILE *f, void *p, unsigned long n)
{
    if (n == 0)
        return 0;
    return fread(p, 1, n, f) == n ? 0 : -1;
}

/* Scalar (int/enum/float) with little-endian conversion. */
static int sm_write_scalar(FILE *f, const void *p, unsigned long n)
{
    if (n == 0)
        return 0;
    if (n == 1 || sm_is_little_endian())
        return sm_write_raw(f, p, n);
    {
        unsigned char *tmp = (unsigned char *)malloc(n);
        int rc;
        if (!tmp)
            return -1;
        memcpy(tmp, p, n);
        sm_swap_bytes(tmp, n);
        rc = sm_write_raw(f, tmp, n);
        free(tmp);
        return rc;
    }
}

static int sm_read_scalar(FILE *f, void *p, unsigned long n)
{
    if (sm_read_raw(f, p, n) != 0)
        return -1;
    if (n > 1 && !sm_is_little_endian())
        sm_swap_bytes(p, n);
    return 0;
}

/* Core over metadata: r describes *(const void*)base. */
static int sm_write_value(FILE *f, const void *base, reflect_t *r);
static int sm_read_value(FILE *f, void *base, reflect_t *r);

static int sm_write_value(FILE *f, const void *base, reflect_t *r)
{
    const unsigned char *p;
    unsigned long i;

    if (!f || !base || !r)
        return -1;
    p = (const unsigned char *)base;
    switch (r->kind)
    {
    case 2: /* integer */
    case 8: /* enum: by size */
    case 3: /* float: bitwise copy */
        return sm_write_scalar(f, p, r->size);
    case 5: /* array */
        if (!r->elem || r->elem->size == 0)
            return -1;
        for (i = 0; i < r->count; i++)
            if (sm_write_value(f, p + i * r->elem->size, r->elem) != 0)
                return -1;
        return 0;
    case 6: /* struct: field by field (no padding) */
    case 7: /* union: largest field only (covers storage) */
        if (!r->fields)
            return r->count == 0 ? 0 : -1;
        if (r->kind == 7)
        {
            unsigned long best = 0, bi = 0;
            for (i = 0; i < r->count; i++)
                if (r->fields[i].type
                    && r->fields[i].type->size > best)
                {
                    best = r->fields[i].type->size;
                    bi = i;
                }
            if (best == 0)
                return 0;
            return sm_write_value(f, p + r->fields[bi].offset,
                                  r->fields[bi].type);
        }
        for (i = 0; i < r->count; i++)
        {
            if (!r->fields[i].type)
                return -1;
            if (sm_write_value(f, p + r->fields[i].offset,
                               r->fields[i].type) != 0)
                return -1;
        }
        return 0;
    case 10: /* typedef: underlying type */
        return r->elem ? sm_write_value(f, p, r->elem) : -1;
    default: /* 0, 1, 4, 9: void/pointers/functions don't serialize */
        return -1;
    }
}

static int sm_read_value(FILE *f, void *base, reflect_t *r)
{
    unsigned char *p;
    unsigned long i;

    if (!f || !base || !r)
        return -1;
    p = (unsigned char *)base;
    switch (r->kind)
    {
    case 2:
    case 8:
    case 3:
        return sm_read_scalar(f, p, r->size);
    case 5:
        if (!r->elem || r->elem->size == 0)
            return -1;
        for (i = 0; i < r->count; i++)
            if (sm_read_value(f, p + i * r->elem->size, r->elem) != 0)
                return -1;
        return 0;
    case 6:
    case 7:
        if (!r->fields)
            return r->count == 0 ? 0 : -1;
        if (r->kind == 7)
        {
            unsigned long best = 0, bi = 0;
            for (i = 0; i < r->count; i++)
                if (r->fields[i].type
                    && r->fields[i].type->size > best)
                {
                    best = r->fields[i].type->size;
                    bi = i;
                }
            if (best == 0)
                return 0;
            return sm_read_value(f, p + r->fields[bi].offset,
                                 r->fields[bi].type);
        }
        for (i = 0; i < r->count; i++)
        {
            if (!r->fields[i].type)
                return -1;
            if (sm_read_value(f, p + r->fields[i].offset,
                              r->fields[i].type) != 0)
                return -1;
        }
        return 0;
    case 10:
        return r->elem ? sm_read_value(f, p, r->elem) : -1;
    default:
        return -1;
    }
}

/* Core without generics (works under any -std). */
static int sm_save_r(FILE *f, const void *p, reflect_t *r)
{
    return sm_write_value(f, p, r);
}

static int sm_load_r(FILE *f, void *p, reflect_t *r)
{
    return sm_read_value(f, p, r);
}

/* CZet generic shortcuts. */
int sm_save<T>(FILE *f, T *v)
{
    reflect_t r;
    if (!f || !v)
        return -1;
    r = _Reflect(*v);
    return sm_write_value(f, (const void *)v, &r);
}

int sm_load<T>(FILE *f, T *v)
{
    reflect_t r;
    if (!f || !v)
        return -1;
    r = _Reflect(*v);
    return sm_read_value(f, (void *)v, &r);
}

#endif /* CZET_UTILS_SAVE_MEMORY_H */
