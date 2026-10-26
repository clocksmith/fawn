#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile ordinary consumers using only a moved host artifact and public header."""
from __future__ import annotations

import argparse
import json
import hashlib
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = ROOT.parents[2]


def invoke(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    environment = dict(os.environ)
    environment.pop("LD_LIBRARY_PATH", None)
    environment.pop("DYLD_LIBRARY_PATH", None)
    return subprocess.run(command, cwd=cwd, env=environment, check=True,
                          capture_output=True, text=True, timeout=120)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc", help="C compiler command")
    parser.add_argument("--cxx", default="c++", help="C++ compiler command")
    parser.add_argument("--library", required=True, type=Path,
                        help="Built allocating host companion")
    args = parser.parse_args()
    library = args.library.resolve()
    with tempfile.TemporaryDirectory(prefix="doe-host-install-") as directory:
        work = Path(directory)
        original = work / "first staging location"
        for name in ("include", "lib", "bin"):
            (original / name).mkdir(parents=True)
        shutil.copy2(ROOT / "include/doe_compiler_host.h", original / "include")
        shutil.copy2(library, original / "lib")
        for name in ("doe-tsir-bootstrap-manifest-inputs-c", "doe-tsir-bootstrap-oracle-c"):
            shutil.copy2(ROOT / "build" / name, original / "bin")
        # No old location survives: loader success cannot come from that path.
        moved = work / "moved host artifact"
        original.rename(moved)
        rpath = "@loader_path/../lib" if sys.platform == "darwin" else "$ORIGIN/../lib"
        for filename, compiler, standard in (
            ("test_compiler_host.c", args.cc, "-std=c11"),
            ("test_compiler_host.cpp", args.cxx, "-std=c++17"),
        ):
            source = work / filename
            shutil.copy2(ROOT / "tests" / filename, source)
            binary = moved / "bin" / ("consumer-cpp" if filename.endswith(".cpp") else "consumer-c")
            command = [*shlex.split(compiler), standard, "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                       "-I" + str(moved / "include"), str(source),
                       str(moved / "lib" / library.name), "-Wl,-rpath," + rpath,
                       "-o", str(binary)]
            invoke(command, work)
            arguments = [str(binary)]
            if filename.endswith(".c"):
                arguments.append(str(REPOSITORY))
            print(invoke(arguments, work).stdout, end="")
        manifest = moved / "bin/doe-tsir-bootstrap-manifest-inputs-c"
        rows = json.loads(invoke([str(manifest)], REPOSITORY).stdout)
        expected = {(kernel, backend) for kernel in ("fused_gemv", "rms_norm", "gather")
                    for backend in ("webgpu-generic", "msl", "spir-v")}
        actual = {(row["kernelRef"].removeprefix("doe.tsir.bootstrap."), row["backend"])
                  for row in rows}
        assert len(rows) == 9 and actual == expected
        inputs = work / "gather.json"
        inputs.write_text(json.dumps({"kernel": "gather", "inputs": {
            "indices": {"elem": "u32", "shape": [2], "values": [1, 0]},
            "table": {"elem": "f32", "shape": [2, 2], "values": [1, 2, 3, 4]},
        }}), encoding="utf-8")
        oracle = moved / "bin/doe-tsir-bootstrap-oracle-c"
        result = json.loads(invoke([str(oracle), "--kernel", "gather",
                                   "--inputs", str(inputs)], work).stdout)
        assert result["status"] == "pass"
        assert result["referenceHash"] == hashlib.sha256(
            struct.pack("<4f", 3, 4, 1, 2)).hexdigest()
        print("PASS: moved public C/C++ host artifact and native manifest/oracle CLIs, no loader override")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
