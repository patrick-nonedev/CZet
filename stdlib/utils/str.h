/* SPDX-License-Identifier: GPL-3.0-or-later WITH GCC-exception-3.1 */
/* Minimal growing string builder.
 *
 *   struct str s;
 *   str_init(&s);
 *   str_push_cstr(&s, "hello ");
 *   str_push_fmt(&s, "%d", 42);
 *   printf("%s\n", str_cstr(&s));
 *   str_free(&s);
 *
 * Header-only `static`.  int functions return 0 on success, -1 on OOM.
 */
#ifndef CZET_UTILS_STR_H
#define CZET_UTILS_STR_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct str
{
    char *data;
    unsigned long len;
    unsigned long cap;
};

static int str_init(struct str *s)
{
    if (!s)
        return -1;
    s->data = (char *)0;
    s->len = 0;
    s->cap = 0;
    return 0;
}

static int str_reserve(struct str *s, unsigned long extra)
{
    unsigned long need;
    char *nd;

    if (!s)
        return -1;
    need = s->len + extra + 1;
    if (need <= s->cap)
        return 0;
    {
        unsigned long ncap = s->cap ? s->cap : 32;
        while (ncap < need)
            ncap *= 2;
        nd = (char *)realloc(s->data, ncap);
        if (!nd)
            return -1;
        s->data = nd;
        s->cap = ncap;
    }
    return 0;
}

static int str_push_n(struct str *s, const char *p, unsigned long n)
{
    if (!s || (!p && n > 0))
        return -1;
    if (n == 0)
        return 0;
    if (str_reserve(s, n) != 0)
        return -1;
    memcpy(s->data + s->len, p, n);
    s->len += n;
    s->data[s->len] = '\0';
    return 0;
}

static int str_push_cstr(struct str *s, const char *cstr)
{
    if (!cstr)
        return -1;
    return str_push_n(s, cstr, (unsigned long)strlen(cstr));
}

static int str_push_char(struct str *s, char c)
{
    return str_push_n(s, &c, 1);
}

static int str_push_fmt(struct str *s, const char *fmt, ...)
{
    va_list ap;
    va_list aq;
    int need;

    if (!s || !fmt)
        return -1;
    va_start(ap, fmt);
    va_copy(aq, ap);
    need = vsnprintf((char *)0, 0, fmt, aq);
    va_end(aq);
    if (need < 0)
    {
        va_end(ap);
        return -1;
    }
    if (str_reserve(s, (unsigned long)need) != 0)
    {
        va_end(ap);
        return -1;
    }
    vsnprintf(s->data + s->len, (size_t)((unsigned long)need + 1), fmt, ap);
    va_end(ap);
    s->len += (unsigned long)need;
    return 0;
}

/* NUL-terminated pointer ("" when empty or after a past OOM). */
static const char *str_cstr(struct str *s)
{
    if (!s)
        return "";
    if (!s->data)
        return "";
    return s->data;
}

static void str_clear(struct str *s)
{
    if (!s)
        return;
    s->len = 0;
    if (s->data)
        s->data[0] = '\0';
}

static void str_free(struct str *s)
{
    if (!s)
        return;
    free(s->data);
    s->data = (char *)0;
    s->len = 0;
    s->cap = 0;
}

#endif /* CZET_UTILS_STR_H */
