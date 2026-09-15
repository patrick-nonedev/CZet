<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# third_party — vendored pristine sources

This directory holds **only the pristine tarballs** (SHA256-pinned in
`SHA256SUMS`) plus one documented minimal patch. Nothing is hand-edited:
the `Makefile` extracts them under `build/third_party/` when assembling
the blob.

| Package | Origin | Pinned commit | License | Status in CZet |
|---|---|---|---|---|
| `libaco` | https://github.com/hnes/libaco | `d00631a9` | Apache-2.0 | Integrated: `aco.c`+`acosw.S` → `libczet_utils.a`, headers `<utils/libaco.h>` + `<utils/aco.h>` |
| `critbit` (agl/djb) | https://github.com/agl/critbit | `4bb6990` | Public domain (djb) | Integrated: `critbit.c` → `libczet_utils.a`, header `<utils/critbit.h>` + patch `critbit-cplusplus-guard.patch` |
| `btree.c` (tidwall) | https://github.com/tidwall/btree.c | `1115041` | MIT | Integrated: `btree.c` → `libczet_utils.a`, header `<utils/btree.h>` |
| `ck` (ConcurrencyKit) | https://github.com/concurrencykit/ck | `b5475f5` | BSD-2-Clause | Downloaded and pinned; **integration pending** (needs its `configure` run per libc in the blob step) |
| `userspace-rcu` | https://github.com/urcu/userspace-rcu | `645fdc9` | LGPL-2.1 | Downloaded and pinned; **integration pending** (git tarball ships no `configure`: needs `autoreconf`, not yet in `shell.nix`) |

Upstream licenses apply to their files; see `../LICENSE`.

## Why Abseil / cwisstable is missing

Upstream `cwisstable` is part of **Abseil, which is C++**, and CZet is a
C-only compiler (`--enable-languages=c`): Abseil can neither compile nor
link here, so vendoring it would be dead weight. The need is covered in C
by `<utils/hashmap.h>` (plain tables) and `<utils/btree.h>` (ordered). To
keep the sources as reference anyway, extend the fetch script later.

## critbit patch

Upstream `critbit.h` ships `extern "C" { ... };` **without** `#ifdef
__cplusplus`, which is a syntax error in C. `critbit-cplusplus-guard.patch`
only adds the two guards; the `.c` stays 100% pristine.

## Re-download / verify

```sh
tools/fetch-third-party.sh   # fetches the 5 tarballs, checks SHA256SUMS
```
