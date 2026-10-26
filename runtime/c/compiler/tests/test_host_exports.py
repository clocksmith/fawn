#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the companion's explicit exports and the unchanged no-heap core."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def symbols(binary: Path, defined: bool) -> set[str]:
    command = (["nm", "-gU" if defined else "-gu", str(binary)]
               if sys.platform == "darwin" else
               ["nm", "-D", "--defined-only" if defined else "--undefined-only",
                str(binary)])
    result = subprocess.run(
        command, check=True, capture_output=True, text=True, timeout=30,
    )
    result_names = {line.split()[-1].split("@")[0]
                    for line in result.stdout.splitlines() if line.strip()}
    return ({name.removeprefix("_") for name in result_names}
            if sys.platform == "darwin" else result_names)


def main() -> int:
    binary = Path(sys.argv[1]).resolve()
    spec = importlib.util.spec_from_file_location(
        "host_exports", ROOT / "tools/export_symbols.py")
    assert spec and spec.loader
    exports = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(exports)
    expected = set(exports.symbols((ROOT / "host/exports.map").read_text(encoding="utf-8")))
    assert symbols(binary, True) == expected
    core = ROOT / "build/libdoe_compiler.so"
    assert symbols(core, True) == set(exports.symbols(
        (ROOT / "src/exports.map").read_text(encoding="utf-8")))
    allocators = {"malloc", "calloc", "realloc", "free", "aligned_alloc"}
    assert symbols(core, False).isdisjoint(allocators)
    assert "free" in symbols(binary, False)
    print("PASS: bounded host exports; original compiler exports/no-heap imports preserved")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
