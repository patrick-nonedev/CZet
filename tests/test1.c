/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

/* C23: nullptr, typeof, typeof_unqual, auto */
static int add(int a, int b)
{
    return a + b;
}

#define TYPECHECK(expr, type) \
    _Static_assert(_Generic((expr), type: 1, default: 0))

static int square(int x)
{
    return x * x;
}

/* GNU statement expression */
#define MAX(a, b) ({             \
    __auto_type _a = (a);       \
    __auto_type _b = (b);       \
    _a > _b ? _a : _b;          \
})

/* GNU attribute syntax */
static int __attribute__((const))
twice(int x)
{
    return x * 2;
}

/* C23 enum underlying values */
enum numbers {
    ZERO,
    ONE,
    TWO,
    BIG = 1000000
};

/* Flexible array member */
struct packet {
    uint32_t size;
    unsigned char data[];
};

/* GNU designated range initializer */
static const unsigned char table[256] = {
    [0 ... 255] = 0xAA,
    ['A'] = 0x41,
    ['Z'] = 0x5A
};

/* C23 nullptr */
static void check_null(void)
{
    int *p = nullptr;
    assert(p == nullptr);
}

/* GNU typeof */
static void check_types(void)
{
    int x = 42;
    typeof(x) y = 10;

    TYPECHECK(x, int);
    TYPECHECK(y, int);

    typeof_unqual(const int) z = 20;
    (void)z;
}

/* C23 binary literals + digit separators */
static void check_constants(void)
{
    int a = 0b101010;
    int b = 1'000'000;

    assert(a == 42);
    assert(b == 1000000);
}

int main(void)
{
    check_null();
    check_types();
    check_constants();

    assert(add(2, 3) == 5);
    assert(square(7) == 49);
    assert(MAX(10, 20) == 20);
    assert(MAX(20, 10) == 20);
    assert(twice(21) == 42);

    struct packet *p =
        __builtin_alloca(sizeof(*p) + 16);

    p->size = 16;

    for (size_t i = 0; i < p->size; ++i)
        p->data[i] = table[i];

    assert(p->data[0] == 0xAA);

    puts("frontend/language test: OK");
    return 0;
}
