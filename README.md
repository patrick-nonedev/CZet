<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<p align="center">
  <img src="assets/CZet.png" alt="CZet logo" width="220" />
</p>

# CZet
A more powerful C without C++.

## Documentation

User guides live in [`docs/`](docs/README.md):

- Macros `_Macro`: [`docs/GUIA_MACROS.md`](docs/GUIA_MACROS.md) (ES) ·
  [`docs/CZET_MACRO_GUIDE.md`](docs/CZET_MACRO_GUIDE.md) (EN)
- Reflection `_Reflect` → `reflect_t`: [`docs/GUIA_REFLECT.md`](docs/GUIA_REFLECT.md) (ES) ·
  [`docs/CZET_REFLECT_GUIDE.md`](docs/CZET_REFLECT_GUIDE.md) (EN)
- Standard library `<utils/...>`: [`docs/GUIA_STDLIB.md`](docs/GUIA_STDLIB.md) (ES) ·
  [`docs/CZET_STDLIB_GUIDE.md`](docs/CZET_STDLIB_GUIDE.md) (EN)

## Philosophy
CZet is a language inspired by C — in fact, all ISO C code compiles as CZet. CZet just adds the features C cannot have without modifying the compiler. CZet aims to be a more powerful C without the complexity and overhead of C++.

## What's new?
- Compile-time               (constexpr just like in C23 but with function support)
- Generics                   (monomorphized `T f<T>(...)`, `struct box<T,U>`)
- `defer`                    (`defer { ... };` runs at function exit)
- Reflection                 (`_Reflect(T)` returns a primitive `reflect_t`)
- New macros                 (everything the C preprocessor does and more)
    - Pattern matching       (it distinguishes between memory, declarations, function calls and operations)
    - New syntax, more flexible
- Operator overloading       (just a more comfortable way to write a function)

---
The syntax is the same — obviously it is compatible with ISO C — and it provides all the tools you need to build *everything* without paying for C++ abstraction cost.

*NOTE*: The suggested extensions are `.slt` for implementations and `.hlt` for headers

---

## Standard library (`<utils/...>`)

CZet ships its own stdlib **inside the binary**: `#include <utils/name.h>` with **no extra flags**
(the only exception is `thread_pool.h`, which needs explicit `-pthread` on glibc).
Full guides: [`docs/GUIA_STDLIB.md`](docs/GUIA_STDLIB.md) (ES) ·
[`docs/CZET_STDLIB_GUIDE.md`](docs/CZET_STDLIB_GUIDE.md) (EN).
Each module is covered by a test in [`tests/stdlib/`](tests/stdlib/):

| Module | Header | Test | What the test exercises |
|---|---|---|---|
| Arena allocator | `<utils/arena.h>` | [`arena.slt`](tests/stdlib/arena.slt) | Linear bump allocator: `arena_init/alloc/calloc`, `arena_used/capacity`, alignment handling (rejects non-power-of-two), `arena_reset/free` |
| Growing string | `<utils/str.h>` | [`str.slt`](tests/stdlib/str.slt) | String builder: `str_init/push_cstr/push_char/push_fmt`, auto-growth, `str_cstr/clear/free` |
| Borrowed slice | `<utils/slice.h>` | [`slice.slt`](tests/stdlib/slice.slt) | Generic view `struct slice<T>`: `slice_make/len/at/sub`, bounds checking (`NULL` out of range, clamped sub-slices), write-through semantics |
| Hash map | `<utils/hashmap.h>` | [`hashmap.slt`](tests/stdlib/hashmap.slt) | Open-addressed `string → void*` map (FNV-1a, linear probing): 500 keys, rehash, `hmap_put/get/del/count/free`, tombstone reuse |
| Ordered btree | `<utils/btree.h>` | [`btree.slt`](tests/stdlib/btree.slt) | Vendored tidwall btree: `btree_new/set/get/delete/min/max/count`, ordered iteration (`btree_iter_first/next/item`), `btree_free` |
| Crit-bit trie | `<utils/critbit.h>` | [`critbit.slt`](tests/stdlib/critbit.slt) | Vendored agl/djb critbit: `critbit0_insert/contains/delete/clear/allprefixed`, duplicate-insert (`2` new / `1` existing) and prefix search |
| Binary persistence | `<utils/save_memory.h>` | [`save_memory.slt`](tests/stdlib/save_memory.slt) | Endian-aware packed save/load via `_Reflect` + generics: `sm_save<T>/sm_load<T>` and `sm_save_r/sm_load_r`, structs/unions/arrays/typedefs, pointers rejected |
| Double buffering | `<utils/double_buffering.h>` | [`dbuf.slt`](tests/stdlib/dbuf.slt) | Lock-free SPSC double buffer with generics (`struct dbuf<T>`): `dbuf_init/write/read`, latest-frame-wins |
| Triple buffering | `<utils/triple_buffering.h>` | [`tbuf.slt`](tests/stdlib/tbuf.slt) | Lock-free SPSC triple buffer (`struct tbuf<T>`): `tbuf_init/write/read`, spare slot so a lagging reader keeps the newest datum |
| Thread pool | `<utils/thread_pool.h>` | [`thread_pool.slt`](tests/stdlib/thread_pool.slt) | pthread pool: `pool_create(4, 64)`, 200× `pool_submit`, `pool_wait/destroy` (needs `-pthread` on glibc, none on static musl) |
| Coroutines | `<utils/aco.h>` | [`aco.slt`](tests/stdlib/aco.slt) | Cooperative coroutines over libaco: `co_sched_init/spawn/run/free`, `co_yield` round-robin interleaving |
| TCP networking | `<utils/server.h>` | [`server.slt`](tests/stdlib/server.slt) | Minimal TCP (IPv4/IPv6): `tcp_listen/connect/accept`, `tcp_send_all/recv_all`, `tcp_port` (ephemeral port `"0"`), `tcp_close` |
| UNIX IPC | `<utils/ipc.h>` | [`ipc.slt`](tests/stdlib/ipc.slt) | AF_UNIX `SOCK_STREAM` IPC: `ipc_server_open/close`, `ipc_client_open/close`, `ipc_send_all/recv_all` |

```text
czet -static tests/stdlib/str.slt -o str && ./str
czet -static tests/stdlib/slice.slt -o slice && ./slice
czet -static -pthread tests/stdlib/thread_pool.slt -o pool && ./pool
```

---

## Technical overview

CZet is a self-contained compiler driver built on top of a stripped-down
GCC. The single `czet` binary embeds the whole toolchain **and** two
self-contained libc cross-roots (musl and glibc), so it does **not** depend on
the host's `/usr/include`, on a system compiler, or on NixOS store paths. It
assumes you are on a normal Linux distribution, but it also runs on NixOS.

### What the binary contains

The final executable is laid out as:

```
[ static C driver ] [ blob (tar ustar) ] [ trailer: magic + off + size + hash ]
```

- The **driver** is a tiny static C program (`src/czet.c`).
- The **blob** is an uncompressed `ustar` tar that holds:
  - `gcc/`    — the trimmed GCC toolchain for C (`xgcc`, `cc1`, `collect2`,
    `liblto_plugin.so`, `crtbegin*`, `crtend*`, and GCC's own headers such as
    `stddef.h`/`stdarg.h`/`float.h`).
  - `libgcc/` — `libgcc.a` and `libgcc_eh.a`.
  - `sysroot/musl/`  — musl 1.2.5 headers + static crt + `libc.a`.
  - `sysroot/glibc/` — glibc headers + crt + static libs (`libc.a`, `libm.a`, …).
- The **trailer** stores the blob offset, its byte size, and an FNV-1a 64 hash
  used to identify the extracted version.

### How it runs (self-extraction)

1. On startup the driver resolves its own executable via `/proc/self/exe`,
   reads the trailer, and locates the blob.
2. It extracts the blob to a per-version cache directory,
   `$XDG_CACHE_HOME/czet/<hash>/` (falls back to `~/.cache`). If that version
   is already present and marked, extraction is skipped.
3. It builds the `xgcc` command line for the selected libc and `execv`s it.
   `as` and `ld` are taken from the host's `PATH` (binutils); the compiler and
   libc come entirely from the embedded blob.

### Selecting the libc

```
czet -libc=musl    ...    # default: static musl (cross-portable binaries)
czet -libc=glibc   ...    # static glibc
czet -libc=/path/sysroot  # custom sysroot with include/ and lib/
czet --extract-only       # extract the blob to cache and print its path
```

- `-std=czet` is the default when no `-std` is passed (use `-std=gnu23` to disable CZet extensions).
- The default is **musl** because a static musl binary runs on essentially any
  Linux without extra runtime.
- glibc is embedded as a **static** cross-root (headers + `libc.a` + crt), so
  both libc options produce self-contained static executables.

### What we stripped or changed in GCC

- Languages: **C only** (`--enable-languages=c`), no C++/LTO for the embedded
  blob. `lto1` and `lto-dump` are intentionally omitted; `liblto_plugin.so` is
  kept because `xgcc` needs it even for plain `cc1` invocations.
- Build flags: `--disable-nls --disable-bootstrap --disable-multilib
  --disable-werror --disable-checking`.
- `--with-native-system-header-dir` points at the real glibc headers so the
  `stmp-fixinc` stage does not fail on NixOS (there is no `/usr/include`).
- The toolchain (`xgcc`/`cc1`/`collect2`) is taken from `./build` and embedded
  as-is; it is dynamic against nix-store glibc at *build* time, but at *run*
  time all the C library comes from the embedded sysroots selected by `-libc=`.

### Building

- `make build`  — configure + build the trimmed GCC tree into `./build`.
- `make czet` — compile the static driver, assemble the blob
  (`./build/blob-root` → `./build/czet.blob.tar`), and sew everything into
  the final `./czet` (≈ 450 MB; the embedded `cc1` is ~400 MB by itself).

The Makefile resolves the glibc/musl store paths automatically on NixOS,
picking the x86-64 variants, and filters out 32-bit glibc roots. On a normal
distro those variables can be overridden on the command line.

## License

CZet is free software under **GPL-3.0-or-later** (see `LICENSE`; GCC
sources under `src/gcc/` keep their own GPL notices).  Programs *you*
compile with CZet are **not** covered: `libgcc` and the CZet runtime
headers (`stdlib/utils`) carry the GCC Runtime Library Exception
(`src/gcc/COPYING.RUNTIME`), and `libczet_utils.a` bundles only
permissively licensed code (Apache-2.0/MIT/public domain).
