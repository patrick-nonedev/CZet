#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Append a ustar tar blob to a czet binary.

Final layout:  [base binary] [blob] [32B trailer]
trailer = magic "CZETAR" (8B) | u64le offset | u64le size | u64le hash(FNV-1a 64)

usage: embed.py <base-bin> <blob.tar> <out-bin>
"""
import sys


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def le64(v: int) -> bytes:
    return int.to_bytes(v, 8, "little")


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    _, base_path, blob_path, out_path = sys.argv

    with open(base_path, "rb") as f:
        base = f.read()
    with open(blob_path, "rb") as f:
        blob = f.read()
    if len(blob) == 0:
        print("error: empty blob")
        return 1

    off = len(base)
    size = len(blob)
    h = fnv1a64(blob)

    trailer = b"CZETAR\0\0" + le64(off) + le64(size) + le64(h)  # 8+24=32
    assert len(trailer) == 32

    with open(out_path, "wb") as f:
        f.write(base)
        f.write(blob)
        f.write(trailer)

    print(f"ok: {out_path} = {len(base)}+{size} bytes, off={off} "
          f"hash={h:016x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())