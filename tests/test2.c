/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static atomic_int counter = 0;

static int worker(void *arg)
{
    int n = *(int *)arg;

    for (int i = 0; i < n; ++i)
        atomic_fetch_add_explicit(
            &counter, 1, memory_order_relaxed);

    return 0;
}

static int fibonacci(int n)
{
    if (n < 2)
        return n;

    return fibonacci(n - 1) + fibonacci(n - 2);
}

static int test_generic(void)
{
    int x = 10;
    double y = 3.5;

#define TYPE_ID(x) _Generic((x), \
        int: 1,                  \
        unsigned: 2,             \
        double: 3,               \
        default: -1)

    assert(TYPE_ID(x) == 1);
    assert(TYPE_ID(y) == 3);

    return 0;
}

static int test_memory(void)
{
    uint8_t src[] = { 1, 2, 3, 4, 5, 6 };
    uint8_t dst[ARRAY_LEN(src)];

    memcpy(dst, src, sizeof(src));

    assert(memcmp(src, dst, sizeof(src)) == 0);

    return 0;
}

static int test_threads(void)
{
    enum {
        THREADS = 4,
        ITERATIONS = 100000
    };

    thrd_t threads[THREADS];

    atomic_store(&counter, 0);

    for (int i = 0; i < THREADS; ++i)
        assert(thrd_create(&threads[i], worker,
                           &(int){ ITERATIONS }) == thrd_success);

    for (int i = 0; i < THREADS; ++i)
        assert(thrd_join(threads[i], NULL) == thrd_success);

    assert(atomic_load(&counter) ==
           THREADS * ITERATIONS);

    return 0;
}

int main(void)
{
    assert(fibonacci(0) == 0);
    assert(fibonacci(1) == 1);
    assert(fibonacci(10) == 55);
    assert(fibonacci(20) == 6765);

    assert(test_generic() == 0);
    assert(test_memory() == 0);
    assert(test_threads() == 0);

    puts("runtime/optimization test: OK");
    return 0;
}
