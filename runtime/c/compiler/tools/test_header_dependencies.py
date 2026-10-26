#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise real incremental object/archive rebuilds in an isolated owner copy."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
OBJECTS = (
    "build/host-core/src/lex.o", "build/tsir/tsir_source.o",
    "build/tsir-fault/tsir_schema.o", "build/host/tsir.o",
    "build/dxil/dxil_source.o",
)
ARCHIVES = ("build/libdoe_compiler_core.a", "build/libdoe_tsir.a", "build/libdoe_dxil.a")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc", help="Actual C compiler command")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="doe-header-deps-") as directory:
        parent = Path(directory)
        owner = parent / "compiler"
        owner.mkdir()
        shutil.copy2(ROOT.parent / "build-root.mk", parent / "build-root.mk")
        for name in ("Makefile", "src/sources.txt", "src/tsir/tests/oracle_alloc.h"):
            destination = owner / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, destination)
        for name in ("include", "src", "src/tsir", "src/dxil", "targets",
                     "targets/metal", "targets/hlsl", "host"):
            destination = owner / name
            destination.mkdir(parents=True, exist_ok=True)
            for source in (ROOT / name).iterdir():
                if source.is_file() and source.suffix in {".c", ".h", ".mk", ".map"}:
                    shutil.copy2(source, destination / source.name)
        old = time.time_ns() - 120_000_000_000
        for source in owner.rglob("*"):
            if source.is_file():
                os.utime(source, ns=(old, old))
        command = ["make", "-C", str(owner), "-j1", "CC=" + args.cc,
                   "HOST_CORE_OBJECTS=" + OBJECTS[0],
                   "TSIR_TOOL_OBJECTS=" + OBJECTS[1],
                   "DXIL_HOST_OBJECTS=" + OBJECTS[4],
                   *ARCHIVES, OBJECTS[2], OBJECTS[3]]

        def build() -> str:
            return subprocess.run(command, check=True, capture_output=True,
                                  text=True, timeout=120).stdout

        build()
        outputs = [owner / name for name in (*OBJECTS, *ARCHIVES)]
        assert all(path.is_file() and path.stat().st_size > 0 for path in outputs)
        unchanged = {path: path.stat().st_mtime_ns for path in outputs}
        build()
        assert all(path.stat().st_mtime_ns == stamp for path, stamp in unchanged.items())
        for header in ("src/internal.h", "include/doe_compiler.h",
                       "src/new_private_dependency.h", "include/new_public_dependency.h"):
            # Deterministic timestamp ordering, without touching the live tree,
            # sleeping, relying on future mtimes, or forcing make with -B.
            older = time.time_ns() - 60_000_000_000
            for source in owner.rglob("*"):
                if source.is_file() and "build" not in source.relative_to(owner).parts:
                    os.utime(source, ns=(old, old))
            for path in outputs:
                os.utime(path, ns=(older, older))
            changed = owner / header
            with changed.open("a", encoding="utf-8") as stream:
                stream.write("\n/* isolated incremental header edit */\n")
            stamp = time.time_ns() - 30_000_000_000
            os.utime(changed, ns=(stamp, stamp))
            commands = build()
            for name in OBJECTS:
                assert "-o " + name in commands, (header, name, commands)
            for name in ARCHIVES:
                assert "ar rcs " + name in commands, (header, name, commands)
            assert all(path.stat().st_mtime_ns > older for path in outputs), header
            print(f"PASS: {header} rebuilt 5 real objects + 3 archives")
            # Preserve a genuine no-change incremental control after each edit.
            unchanged = {path: path.stat().st_mtime_ns for path in outputs}
            build()
            assert all(path.stat().st_mtime_ns == value for path, value in unchanged.items())
        larger = [part + " build/host-core/src/numeric.o"
                  if part.startswith("HOST_CORE_OBJECTS=") else part
                  for part in command]
        subprocess.run(larger, check=True, capture_output=True, text=True, timeout=120)
        members = subprocess.check_output(["ar", "t", str(owner / ARCHIVES[0])], text=True)
        assert "numeric.o" in members.splitlines()
        build()
        members = subprocess.check_output(["ar", "t", str(owner / ARCHIVES[0])], text=True)
        assert members.splitlines() == ["lex.o"], members
        print("PASS: removed source inventory drops stale archive member without cleaning")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
