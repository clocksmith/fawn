#!/usr/bin/env python3
"""CPU reference for ``concurrent_execution_runsingle_u32.wgsl``.

One invocation copies the 1024-word buffer into workgroup memory, runs the
kernel's million-step xor/add recurrence and writes the result to word 0.
Each dispatch starts from the buffer the previous dispatch left behind.
"""

from __future__ import annotations

import argparse
import hashlib
import struct


WORD_COUNT = 1024
WORD_MASK = WORD_COUNT - 1
LOOP_COUNT = 1_000_000
U32_MASK = (1 << 32) - 1


def expected_output(*, dispatch_count: int, initial: bytes | None = None) -> bytes:
    if dispatch_count <= 0:
        raise ValueError("dispatch_count must be positive")
    data = bytearray(initial if initial is not None else bytes(WORD_COUNT * 4))
    if len(data) != WORD_COUNT * 4:
        raise ValueError(f"buffer must hold {WORD_COUNT} u32 words")
    for _ in range(dispatch_count):
        words = list(struct.unpack(f"<{WORD_COUNT}I", data))
        accum = words[0]
        for index in range(LOOP_COUNT):
            accum = ((accum ^ words[(index + accum) & WORD_MASK]) + 123) & U32_MASK
        struct.pack_into("<I", data, 0, accum)
    return bytes(data)


def expected_sha256(*, dispatch_count: int) -> str:
    return hashlib.sha256(expected_output(dispatch_count=dispatch_count)).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dispatch-count", type=int, required=True)
    args = parser.parse_args()
    print(expected_sha256(dispatch_count=args.dispatch_count))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
