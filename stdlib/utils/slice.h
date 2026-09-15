/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Borrowed view (ptr + len) with CZet generics.
 *
 *   struct slice<int> s; slice_make<int>(&s, buf, n);
 *   int *p = 0; slice_at<int>(&s, i, &p);   // NULL if i >= len
 *   struct slice<int> t; slice_sub<int>(&s, off, len, &t); // clamped
 *
 * (CZet generics accept neither `struct slice<T>` returns nor `T*`
 * comparisons in bodies, hence out-params and `void*` conversions.)
 *
 * The slice never owns memory; the lender keeps ownership.
 * Requires -std=czet (the driver default).
 */
#ifndef CZET_UTILS_SLICE_H
#define CZET_UTILS_SLICE_H

#include <string.h>

struct slice<T>
{
    T *data;
    unsigned long len;
};

void slice_make<T>(struct slice<T> *s, T *data, unsigned long len)
{
    if (!s)
        return;
    if (len == 0 || !data)
    {
        s->data = (void *)0;
        s->len = 0;
        return;
    }
    /* T* vs T* never unifies in generic bodies: copy via void*. */
    memcpy(&s->data, &data, sizeof(data));
    s->len = len;
}

unsigned long slice_len<T>(struct slice<T> *s)
{
    return s ? s->len : 0;
}

/* Element i into *out (NULL if out of range). */
void slice_at<T>(struct slice<T> *s, unsigned long i, T **out)
{
    void *addr;
    if (!out)
        return;
    *out = (void *)0;
    if (!s || !s->data || i >= s->len)
        return;
    addr = s->data;
    *out = (void *)((char *)addr + i * sizeof(s->data[0]));
}

/* Sub-slice [off, off+len) into *out, clamped to fit. */
void slice_sub<T>(struct slice<T> *s, unsigned long off, unsigned long len,
                  struct slice<T> *out)
{
    if (!out)
        return;
    out->data = (void *)0;
    out->len = 0;
    if (!s || !s->data || off >= s->len)
        return;
    if (len > s->len - off)
        len = s->len - off;
    {
        void *start = (void *)&s->data[off];
        memcpy(&out->data, &start, sizeof(start));
    }
    out->len = len;
}

#endif /* CZET_UTILS_SLICE_H */
