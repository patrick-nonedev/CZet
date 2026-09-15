#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Re-download the pristine third_party/ tarballs and verify SHA256.
# Usage: tools/fetch-third-party.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/third_party/src"

dl() { # dl <url> <dest>
    echo ">> downloading $2"
    curl -sSL --max-time 300 -o "$SRC/$2" "$1"
}

dl "https://github.com/hnes/libaco/archive/d00631a9e143a8711c0a6e7b603a72b1e379b661.tar.gz" \
   "libaco.tar.gz"
dl "https://github.com/agl/critbit/archive/4bb69901a9813de05331bd3afc5085e00050f701.tar.gz" \
   "critbit.tar.gz"
dl "https://github.com/tidwall/btree.c/archive/111504136b798782374037ffe335b400394f05b4.tar.gz" \
   "btree.c.tar.gz"
dl "https://github.com/concurrencykit/ck/archive/b5475f5b0a0389abdbff71bf78e08cb470a5a688.tar.gz" \
   "ck.tar.gz"
dl "https://github.com/urcu/userspace-rcu/archive/645fdc9b720606440a3b1ed74ad394e8eaad796b.tar.gz" \
   "urcu.tar.gz"

echo ">> verifying SHA256"
(cd "$ROOT/third_party" && sha256sum -c SHA256SUMS)
echo "== OK: third_party up to date =="
