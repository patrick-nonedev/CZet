<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# CZet stdlib guide (`<utils/...>`)

Besides the C libc (musl or glibc via `-libc=`), CZet ships its own
stdlib **inside the binary**: include with `#include <utils/name.h>` and
**no extra flags** (only `thread_pool.h` needs explicit `-pthread`, §9).

```text
czet -static prog.ct -o prog          # no flags needed for utils
czet -static -pthread pool.ct -o pool # thread_pool is the exception
```

Header-only `static` code lives in `stdlib/utils/`; `libaco`, `critbit` and
`btree.c` are the **vendored originals** (see `third_party/README.md`)
prebuilt into `libczet_utils.a` for musl and glibc — the driver links it
automatically. Demos in `tests/stdlib/`.

---

## 1. `utils/ipc.h` — AF_UNIX IPC

Improved `bootstrap/ipc.h` keeping it simple: same functions, but with an
include guard, `int` returns (0 ok, -1 with `errno`), bounded `sun_path`
copy and exact `addrlen` (was `sizeof` + `strcpy`).

| Function | Does |
|---|---|
| `ipc_server_open(path, &srv, &cli)` | creates, binds (after `unlink`), listens, accepts 1 client |
| `ipc_server_close(path, &srv, &cli)` | closes both (set to -1) and unlinks |
| `ipc_client_open(path, &fd)` | connects |
| `ipc_client_close(&fd)` | closes (sets to -1) |
| `ipc_send_all(fd, buf, len)` / `ipc_recv_all(fd, buf, len)` | exactly `len` bytes (retries `EINTR`) |

## 2. `utils/server.h` — minimal TCP (IPv4/IPv6)

```c
int srv = tcp_listen("127.0.0.1", "0", 8); /* "0" = ephemeral port */
int port = tcp_port(srv);
int cli = tcp_accept(srv);
int out = tcp_connect("127.0.0.1", "8080");
tcp_send_all(cli, buf, len); tcp_recv_all(cli, buf, len);
tcp_close(cli);
```

Needs `_POSIX_C_SOURCE >= 200809L` for `getaddrinfo` under strict
`-std=czet`: the header defines it unless already defined. The `*_all`
helpers return 0/-1 like `ipc.h`.

## 3. `utils/slice.h` — borrowed view (`ptr + len`) with generics

```c
struct slice<T> { T *data; unsigned long len; };
struct slice<int> s; slice_make<int>(&s, buf, n);
int *p = 0; slice_at<int>(&s, i, &p);          /* NULL if i >= len */
struct slice<int> t; slice_sub<int>(&s, off, len, &t); /* clamped */
```

Slices never own memory. Implementation note: CZet generics don't yet
support `struct slice<T>` as a return type nor `T*`-vs-`T*` comparisons in
bodies, hence out-params and `void*` conversions.

## 4. `utils/arena.h` — bump allocator with reset

```c
struct arena a; arena_init(&a, 1 << 20);
int *p = arena_alloc(&a, n * sizeof *p, _Alignof(int));
int *z = arena_calloc(&a, n, sizeof *z, _Alignof(int));
arena_reset(&a);   /* recycle everything */
arena_free(&a);
```

`align` 0 = natural (`max_align_t`); must be a power of two. No per-object
free: for scratch memory.

## 5. `utils/str.h` — growing string builder

```c
struct str s; str_init(&s);
str_push_cstr(&s, "hello "); str_push_fmt(&s, "%d", 42);
printf("%s\n", str_cstr(&s));   /* "" when empty */
str_clear(&s); str_free(&s);
```

## 6. `utils/hashmap.h` — open-addressed string → `void*` map

Linear probing, FNV-1a, ×2 resize at 70%. **Keys stored by pointer** (not
duplicated: use literals or arena memory). Don't store `NULL` values
(indistinguishable from "missing").

```c
struct hmap m; hmap_init(&m);
hmap_put(&m, "k", val); hmap_get(&m, "k"); hmap_del(&m, "k");
hmap_count(&m); hmap_free(&m);
```

## 7. `utils/save_memory.h` — endian-aware binary persistence via `_Reflect`

On-disk: little-endian, packed (**no padding**), no pointers, no versioning.
Reader and writer must share layout.

```c
reflect_t r = _Reflect(*v);
sm_save_r(f, v, &r);   /* 0 ok, -1 error */
sm_load_r(f, v, &r);
/* generic shortcuts: */ sm_save<Data>(f, &d); sm_load<Data>(f, &d);
```

Supports integers (incl. `_BitInt`, enums by size), bitwise floats, arrays,
recursive structs, unions (**largest field only**) and typedefs.
Pointers/functions/void → `-1`. Nested structs use the fields `_Reflect`
exposes up to 3 levels (recursive types truncate instead of growing forever).

## 8. `utils/double_buffering.h` and `utils/triple_buffering.h`

SPSC lock-free frame exchange (one atomic byte). `T` must be `memcpy`-able.

```c
struct dbuf<Frame> b; dbuf_init<Frame>(&b, &f0);
dbuf_write<Frame>(&b, &f);   /* producer: publish a copy */
dbuf_read<Frame>(&b, &out);  /* consumer: latest complete frame */
```

Triple buffering adds a spare slot so a lagging reader never loses the
newest datum (`tbuf_*`, same API).

## 9. `utils/thread_pool.h` — pthread pool

The original `bootstrap/thread_pool.h`, now as `<utils/thread_pool.h>`.
**Exception to "no flags": pass `-pthread` explicitly** (musl static
doesn't need it — pthread lives in `libc.a`; glibc does).

```c
pool_t *p = pool_create(4, 64);
pool_submit(p, job, arg);       /* blocks when queue full */
pool_try_submit(p, job, arg);   /* -1 without blocking when full */
pool_wait(p);                   /* until queue empty + 0 active */
pool_destroy(p);
```

## 10. `utils/aco.h` + `utils/libaco.h` — coroutines

`libaco.h` is the verbatim original (hnes/libaco); `aco.h` adds a minimal
round-robin scheduler (one scheduler per thread):

```c
struct co_sched s; co_sched_init(&s);
co_spawn(&s, task, &arg, 0);  /* stack 0 = 1 MiB */
co_run(&s);                   /* until all tasks are done */
co_sched_free(&s);
/* inside a task: co_yield(); */
```

Each task gets its own stack (`save_stack_sz = 0`). A finished task is never
resumed: the trampoline marks `done` and yields.

## 11. Vendored: `utils/btree.h`, `utils/critbit.h`

Verbatim originals (their `.c` already built into `libczet_utils.a`):

- **btree** (tidwall, MIT): `btree_new(sizeof(int), 0, cmp, 0)`,
  `btree_set/get/delete/min/max/count`, iterators
  (`btree_iter_first/next/item`), `btree_free`.
- **critbit** (agl/djb, public domain): `critbit0_insert/contains/delete/
  clear/allprefixed` over NUL strings. Note: `insert` returns **2** when
  inserting, 1 if present, 0 on OOM. The header carries a minimal documented
  patch (`third_party/critbit-cplusplus-guard.patch`): upstream ships
  `extern "C"` without `#ifdef __cplusplus`, which is invalid C.

## 12. Pending (pinned, not integrated)

- **ck** (ConcurrencyKit, BSD) and **userspace-rcu** (LGPL-2.1): tarballs
  pinned in `third_party/src/` + `tools/fetch-third-party.sh`, but their
  build (per-libc `configure` / `autoreconf`) is not yet in the blob step.
- **cwisstable/Abseil**: it is **C++** and CZet is C-only — not
  integrable; covered by `<utils/hashmap.h>` and `<utils/btree.h>`.
- Details in `third_party/README.md`.
