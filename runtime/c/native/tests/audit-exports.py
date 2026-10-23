#!/usr/bin/env python3
"""Audit the actual shared library and exact implemented procedure inventory."""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import re
import subprocess
import sys


def native_exports(library: Path, platform: str) -> set[str]:
    # Apple cctools nm(1): -g external, -U defined, -j names only.
    # https://github.com/apple-oss-distributions/cctools/blob/main/man/nm.1
    command = ["nm", "-gjU"] if platform == "Darwin" else ["nm", "-D", "--defined-only"]
    output = subprocess.check_output(command + [str(library)], text=True)
    symbols = {line.split()[-1] for line in output.splitlines() if line.strip()}
    if platform == "Darwin":
        # Remove the one Mach-O C prefix, not arbitrary private symbol names.
        symbols = {name[1:] if name.startswith("_") else name for name in symbols}
    return symbols


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    parser.add_argument("--runtime", action="store_true", help="also load the unsanitized library and inspect procedure addresses")
    parser.add_argument("--platform", choices=("Linux", "Darwin"),
                        default="Darwin" if sys.platform == "darwin" else "Linux")
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[1]
    header = owner.parents[1] / "zig/vendor/webgpu-headers/webgpu.h"
    declared = set(re.findall(r"WGPU_EXPORT[^;]*?\b(wgpu\w+)\(", header.read_text()))
    inventory = set((owner / "EXPORTED_SYMBOLS.txt").read_text().splitlines())
    # Doe extensions are a separate, explicit inventory: never webgpu.h names,
    # never resolved through wgpuGetProcAddress.
    extensions = set((owner / "EXTENSION_SYMBOLS.txt").read_text().splitlines())
    header_text = (owner / "include/doe_native_programs.h").read_text()
    extension_declared = set(re.findall(r"DOE_NATIVE_EXTENSION[^;]*?\b(doe\w+)\(", header_text))
    procedures = set(re.findall(r'\{"(wgpu\w+)", \(WGPUProc\)\1\}',
                                (owner / "src/procedures.c").read_text()))
    exported = native_exports(args.library, args.platform)
    if (exported != inventory | extensions or procedures != inventory or not inventory <= declared
            or extensions & declared or extensions != extension_declared):
        raise RuntimeError(f"export inventory mismatch: exports={sorted(exported ^ (inventory | extensions))}, "
                           f"procedures={sorted(procedures ^ inventory)}, "
                           f"unpinned={sorted(inventory - declared)}, "
                           f"extensions={sorted(extensions ^ extension_declared)}")
    if args.runtime:
        class StringView(ctypes.Structure):
            _fields_ = [("data", ctypes.c_char_p), ("length", ctypes.c_size_t)]

        library = ctypes.CDLL(str(args.library.resolve()))
        lookup = library.wgpuGetProcAddress
        lookup.argtypes = [StringView]
        lookup.restype = ctypes.c_void_p
        for name in declared | extensions:
            address = lookup(StringView(name.encode(), len(name)))
            expected = ctypes.cast(getattr(library, name), ctypes.c_void_p).value if name in inventory else None
            if address != expected:
                raise RuntimeError(f"incorrect procedure address for {name}")
        print("PASS every implemented procedure address matches; every unprovided name returns NULL")
    print(f"PASS exact exports: {len(exported & declared)} implemented, "
          f"{len(declared - exported)} pinned unprovided, {len(extensions)} listed Doe extensions, "
          "no private exports")


if __name__ == "__main__":
    main()
