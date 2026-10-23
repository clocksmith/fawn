#!/usr/bin/env python3
"""Audit the actual shared library and exact implemented procedure inventory."""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import re
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--runtime", action="store_true", help="also load the unsanitized library and inspect procedure addresses")
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[1]
    header = owner.parents[1] / "zig/vendor/webgpu-headers/webgpu.h"
    declared = set(re.findall(r"WGPU_EXPORT[^;]*?\b(wgpu\w+)\(", header.read_text()))
    inventory = set((owner / "EXPORTED_SYMBOLS.txt").read_text().splitlines())
    procedures = set(re.findall(r'\{"(wgpu\w+)", \(WGPUProc\)\1\}',
                                (owner / "src/procedures.c").read_text()))
    output = subprocess.check_output(["nm", "-D", "--defined-only", str(args.library)], text=True)
    exported = {line.split()[-1] for line in output.splitlines()}
    if exported != inventory or procedures != inventory or not inventory <= declared:
        raise RuntimeError(f"export inventory mismatch: exports={sorted(exported ^ inventory)}, "
                           f"procedures={sorted(procedures ^ inventory)}, "
                           f"unpinned={sorted(inventory - declared)}")
    if args.runtime:
        class StringView(ctypes.Structure):
            _fields_ = [("data", ctypes.c_char_p), ("length", ctypes.c_size_t)]

        library = ctypes.CDLL(str(args.library.resolve()))
        lookup = library.wgpuGetProcAddress
        lookup.argtypes = [StringView]
        lookup.restype = ctypes.c_void_p
        for name in declared:
            address = lookup(StringView(name.encode(), len(name)))
            expected = ctypes.cast(getattr(library, name), ctypes.c_void_p).value if name in inventory else None
            if address != expected:
                raise RuntimeError(f"incorrect procedure address for {name}")
        print("PASS every implemented procedure address matches; every unprovided name returns NULL")
    print(f"PASS exact exports: {len(exported)} implemented, "
          f"{len(declared - exported)} pinned unprovided, no private exports")


if __name__ == "__main__":
    main()
