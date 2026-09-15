<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Guía de la stdlib de CZet (`<utils/...>`)

Además de la libc de C (musl o glibc, a elegir con `-libc=`), CZet trae
una stdlib propia que **viaja dentro del binario**: se incluye con
`#include <utils/nombre.h>` **sin pasar ningún flag** (solo
`thread_pool.h` pide `-pthread` explícito, ver §9).

```text
czet -static prog.slt -o prog          # sin flags para utils
czet -static -pthread pool.slt -o pool # solo thread_pool pide -pthread
```

Todo lo `static` header-only vive en `stdlib/utils/`; `libaco`, `critbit` y
`btree.c` son los **originales vendorizados** (ver `third_party/README.md`)
precompilados a `libczet_utils.a` para musl y glibc — el driver la enlaza
solo. Demos en `tests/stdlib/`.

---

## 1. `utils/ipc.h` — IPC por socket UNIX

Mejora del `bootstrap/ipc.h` original manteniendo la simplicidad: mismas
funciones, pero con guarda de include, `int` de retorno (0 ok, -1 con
`errno`), path acotado a `sun_path` y `addrlen` exacta (antes `sizeof`
entero + `strcpy`).

| Función | Qué hace |
|---|---|
| `ipc_server_open(path, &srv, &cli)` | crea, bindea (tras `unlink`), escucha y acepta 1 cliente |
| `ipc_server_close(path, &srv, &cli)` | cierra ambos (los deja a -1) y hace `unlink` |
| `ipc_client_open(path, &fd)` | conecta |
| `ipc_client_close(&fd)` | cierra (lo deja a -1) |
| `ipc_send_all(fd, buf, len)` / `ipc_recv_all(fd, buf, len)` | exactamente `len` bytes (reintenta `EINTR`) |

## 2. `utils/server.h` — TCP mínimo (IPv4/IPv6)

```c
int srv = tcp_listen("127.0.0.1", "0", 8); /* "0" = puerto efímero */
int port = tcp_port(srv);
int cli = tcp_accept(srv);
int out = tcp_connect("127.0.0.1", "8080");
tcp_send_all(cli, buf, len); tcp_recv_all(cli, buf, len);
tcp_close(cli);
```

Requiere `_POSIX_C_SOURCE >= 200809L` para `getaddrinfo` con `-std=czet`
estricto: el header lo define solo si falta. Los `*_all` devuelven 0/-1
igual que en `ipc.h`.

## 3. `utils/slice.h` — vista `ptr + len` con genéricos

```c
struct slice<T> { T *data; unsigned long len; };
struct slice<int> s; slice_make<int>(&s, buf, n);
int *p = 0; slice_at<int>(&s, i, &p);          /* NULL si i >= len */
struct slice<int> t; slice_sub<int>(&s, off, len, &t); /* recortado */
```

El slice no posee la memoria. Nota de implementación: los genéricos CZet
aún no admiten `struct slice<T>` como retorno ni comparar `T*` con `T*` en
cuerpos (ver sondas en el historial), así que `make`/`sub`/`at` usan
punteros de salida y conversiones por `void*`.

## 4. `utils/arena.h` — bump allocator con reset

```c
struct arena a; arena_init(&a, 1 << 20);
int *p = arena_alloc(&a, n * sizeof *p, _Alignof(int));
int *z = arena_calloc(&a, n, sizeof *z, _Alignof(int));
arena_reset(&a);   /* recicla todo */
arena_free(&a);
```

`align` 0 = natural (`max_align_t`); debe ser potencia de dos. Sin free por
objeto: ideal para memoria temporal.

## 5. `utils/str.h` — string builder que crece

```c
struct str s; str_init(&s);
str_push_cstr(&s, "hola "); str_push_fmt(&s, "%d", 42);
printf("%s\n", str_cstr(&s));   /* "" si vacío */
str_clear(&s); str_free(&s);
```

## 6. `utils/hashmap.h` — mapa abierto string → `void*`

Sondeo lineal, FNV-1a, redimensionado ×2 al 70%. **Las claves se guardan por
puntero** (no se duplican: usa literales o memoria de una arena). No guardes
`NULL` como valor (indistinguible de "ausente").

```c
struct hmap m; hmap_init(&m);
hmap_put(&m, "k", val); hmap_get(&m, "k"); hmap_del(&m, "k");
hmap_count(&m); hmap_free(&m);
```

## 7. `utils/save_memory.h` — binario endian-aware con `_Reflect`

Formato en disco: little-endian, empaquetado (**sin padding**), sin punteros
ni versionado. Lector y escritor deben compartir layout.

```c
reflect_t r = _Reflect(*v);
sm_save_r(f, v, &r);   /* 0 ok, -1 error */
sm_load_r(f, v, &r);
/* atajos genéricos: */ sm_save<Data>(f, &d); sm_load<Data>(f, &d);
```

Soporta enteros (incl. `_BitInt`, enums por tamaño), flotantes bit a bit,
arrays, structs recursivos, unions (**solo el campo más grande**) y
typedefs. Punteros/funciones/void → `-1`. Los structs anidados usan los
campos que `_Reflect` expone hasta 3 niveles (suficiente; los tipos
recursivos truncan en vez de crecer sin fin).

## 8. `utils/double_buffering.h` y `utils/triple_buffering.h`

Intercambio de fotogramas SPSC sin bloqueos (un byte atómico). `T` copiable
con `memcpy`.

```c
struct dbuf<Frame> b; dbuf_init<Frame>(&b, &f0);
dbuf_write<Frame>(&b, &f);   /* productor: publica copia */
dbuf_read<Frame>(&b, &out);  /* consumidor: último completo */
```

El triple buffer añade un slot intermedio: si el lector va con retraso no se
pierde el último dato (`tbuf_*`, misma API).

## 9. `utils/thread_pool.h` — pool sobre pthreads

El `bootstrap/thread_pool.h` original, tal cual, ahora como
`<utils/thread_pool.h>`. **Excepción a lo de "sin flags": aquí `-pthread`
se pasa explícito** (con musl estática no hace falta, va dentro de
`libc.a`; con glibc sí).

```c
pool_t *p = pool_create(4, 64);
pool_submit(p, trabajo, arg);   /* bloquea si la cola llena */
pool_try_submit(p, trabajo, arg); /* -1 sin bloquear si llena */
pool_wait(p);                   /* hasta cola vacía + 0 activas */
pool_destroy(p);
```

## 10. `utils/aco.h` + `utils/libaco.h` — corrutinas

`libaco.h` es el original verbatim (hnes/libaco); `aco.h` añade un
planificador round-robin mínimo (un planificador por hilo):

```c
struct co_sched s; co_sched_init(&s);
co_spawn(&s, tarea, &arg, 0);  /* stack 0 = 1 MiB */
co_run(&s);                    /* hasta que todas marcan done */
co_sched_free(&s);
/* dentro de la tarea: co_yield(); */
```

Cada tarea tiene su propio stack (`save_stack_sz = 0`). Una tarea terminada
jamás se reanuda: la trampilla marca `done` y cede.

## 11. Vendorizados: `utils/btree.h`, `utils/critbit.h`

Originales tal cual (con su `.c` ya compilado en `libczet_utils.a`):

- **btree** (tidwall, MIT): `btree_new(sizeof(int), 0, cmp, 0)`,
  `btree_set/get/delete/min/max/count`, iteradores
  (`btree_iter_first/next/item`), `btree_free`.
- **critbit** (agl/djb, dominio público): `critbit0_insert/contains/delete/
  clear/allprefixed` sobre strings NUL. Ojo: `insert` devuelve **2** si
  inserta, 1 si ya estaba, 0 si OOM. El header lleva un parche mínimo
  documentado (`third_party/critbit-cplusplus-guard.patch`): el original
  trae `extern "C"` sin `#ifdef __cplusplus`, que no compila en C.

## 12. Pendientes (fijados, no integrados)

- **ck** (ConcurrencyKit, BSD) y **userspace-rcu** (LGPL-2.1): tarballs
  fijados en `third_party/src/` + `tools/fetch-third-party.sh`, pero su
  build (configure por libc / autoreconf) aún no está en el paso del blob.
- **cwisstable/Abseil**: es **C++** y CZet es solo-C — no integrable;
  lo cubren `<utils/hashmap.h>` y `<utils/btree.h>`.
- Detalle en `third_party/README.md`.
