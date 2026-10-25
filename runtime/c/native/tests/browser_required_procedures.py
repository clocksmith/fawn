#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the existing Fawn provider loader contract, without launching a browser.

Resolve every required load in the retained Chromium patch through the selected
library. This proves name resolution only, not signature or backend correctness.
The patch remains the inventory owner; this test does not substitute a provider.
"""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import re


class StringView(ctypes.Structure):
    _fields_ = [("data", ctypes.c_char_p), ("length", ctypes.c_size_t)]


def required_names(patch: Path) -> list[str]:
    source = "\n".join(line[1:] for line in patch.read_text().splitlines()
                       if line.startswith("+") and not line.startswith("+++"))
    starts = re.findall(r"if \(!load\(&mDawnProcs\.", source)
    names = re.findall(
        r'if \(!load\(&mDawnProcs\.\w+,\s*"(wgpu\w+)"\)\) \{', source
    )
    if len(names) != len(starts):
        raise ValueError("unrecognized required provider load syntax")
    if not names or len(names) != len(set(names)):
        raise ValueError("empty or duplicate required provider loads")
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    names = required_names(root / "browser/chromium/patches/dawn-doe-runtime.patch")
    library = ctypes.CDLL(str(args.library.resolve(strict=True)))
    lookup = library.wgpuGetProcAddress
    lookup.argtypes = [StringView]
    lookup.restype = ctypes.c_void_p
    missing = []
    for name in names:
        encoded = name.encode("ascii")
        explicit = lookup(StringView(encoded, len(encoded)))
        terminated = lookup(StringView(encoded, ctypes.c_size_t(-1).value))
        if not explicit or explicit != terminated:
            missing.append(name)
    if missing:
        print("FAIL required Fawn provider procedures: " + ", ".join(missing))
        return 1
    print(f"PASS {len(names)} required Fawn procedures resolve with both name lengths")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
