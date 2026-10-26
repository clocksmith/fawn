#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build/check the allocating host DLL with actual Windows SDK and explicit DXC.

Core, TSIR, DXIL and export inventories are shared with their existing owners.
This is source/host/DXC integration, not a D3D12 device qualification runner.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import struct
import sys

from export_symbols import symbols
from source_inventory import compiler_sources
from windows_compile import strict_command

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, type=Path,
                        help="Fresh external output directory")
    parser.add_argument("--clang-cl", default="clang-cl",
                        help="Windows SDK clang-cl executable")
    parser.add_argument("--dxc", required=True, type=Path,
                        help="Actual DXC executable for source-to-toolchain check")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("requires an actual x64 Windows SDK developer environment")
    compiler = shutil.which(args.clang_cl)
    if compiler is None or not args.dxc.is_file():
        parser.error("requires clang-cl and an existing actual DXC executable")
    out = args.out.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error("out must be outside the source owner")
    if out.exists() and any(out.iterdir()):
        parser.error("out must be fresh or empty; preserve previous evidence")
    out.mkdir(parents=True, exist_ok=True)
    sources = (compiler_sources(ROOT) + sorted(ROOT.glob("src/tsir/*.c")) +
               sorted(ROOT.glob("src/dxil/*.c")) + sorted(ROOT.glob("host/*.c")))
    exports = symbols((ROOT / "host/exports.map").read_text(encoding="utf-8"))
    bound = [*sources, ROOT / "host/exports.map", ROOT / "include/doe_compiler_host.h",
             *ROOT.glob("src/tsir/*.h"), *ROOT.glob("src/dxil/*.h"),
             *ROOT.glob("src/*.h"), *ROOT.glob("include/*.h"), Path(__file__),
             ROOT / "tools/windows_compile.py", ROOT / "tools/test_host_dxil.py",
             ROOT / "tools/source_inventory.py", ROOT / "tools/export_symbols.py",
             *ROOT.glob("host/*.h"), *ROOT.glob("targets/*/*.h"),
             ROOT / "tests/test_compiler_host.c", ROOT / "tests/test_compiler_host.cpp",
             ROOT / "src/dxil/tests/test_dxil_io.c",
             ROOT / "tools/tsir_bootstrap_oracle.c", ROOT / "tools/tsir_bootstrap_manifest_inputs.c"]
    identities = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in bound}
    (out / "inputs.json").write_text(json.dumps(identities, indent=2) + "\n", encoding="utf-8")
    commands: list[list[str]] = []

    def invoke(command: list[str], directory: Path = out) -> subprocess.CompletedProcess[str]:
        commands.append(command)
        process = subprocess.Popen(command, cwd=directory, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True,
                                   encoding="utf-8", errors="replace")
        try:
            stdout, stderr = process.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            # In particular, an actual DXC grandchild must not outlive a timed
            # out Python ABI probe. Kill only this builder's owned child tree.
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           check=True, timeout=30, capture_output=True)
            process.communicate(timeout=30)
            raise
        result = subprocess.CompletedProcess(command, process.returncode,
                                             stdout, stderr)
        (out / f"command-{len(commands):03}.log").write_text(
            subprocess.list2cmdline(command) + "\n" + result.stdout + result.stderr,
            encoding="utf-8")
        result.check_returncode()
        return result

    strict = strict_command(compiler)
    objects: list[str] = []
    for source in sources:
        obj = out / (source.relative_to(ROOT).as_posix().replace("/", "_") + ".obj")
        invoke([*strict, "/c", str(source), "/Fo" + str(obj)])
        objects.append(str(obj))
    export_file = out / "host.def"
    export_file.write_text("EXPORTS\n" + "\n".join(exports) + "\n", encoding="utf-8")
    library = out / "doe_compiler_host.dll"
    import_library = out / "doe_compiler_host.lib"
    invoke([compiler, "/nologo", "/LD", *objects, "/Fe" + str(library), "/link",
            "/def:" + str(export_file), "/implib:" + str(import_library)])
    result = invoke(["dumpbin", "/nologo", "/exports", str(library)])
    actual = re.findall(r"^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\w+)\s*$",
                        result.stdout, re.MULTILINE)
    if sorted(actual) != sorted(exports):
        raise ValueError(f"host DLL export map mismatch: {actual}")
    include = "/I" + str(ROOT / "include")
    for source in ("test_compiler_host.c", "test_compiler_host.cpp"):
        executable = out / (Path(source).stem + ("_cpp" if source.endswith(".cpp") else "") + ".exe")
        command = ([compiler, "/nologo", "/MD", "/O2", "/WX", "/std:c++17",
                    "/clang:-fno-exceptions", "/clang:-fno-rtti"]
                   if source.endswith(".cpp") else strict)
        invoke([*command, include, str(ROOT / "tests" / source),
                "/Fo" + str(executable.with_suffix(".obj")), "/Fe" + str(executable),
                "/link", str(import_library), "/stack:8388608"])
        invoke([str(executable), *([] if source.endswith(".cpp") else [str(ROOT.parents[2])])])
    for name in ("tsir_bootstrap_oracle", "tsir_bootstrap_manifest_inputs"):
        executable = out / (name + ".exe")
        invoke([*strict, "/I" + str(ROOT / "src/tsir"),
                str(ROOT / "tools" / (name + ".c")), *objects,
                "/Fe" + str(executable), "/Fo" + str(out / (name + ".obj")),
                "/link", "/stack:8388608"])
    rows = json.loads(invoke([str(out / "tsir_bootstrap_manifest_inputs.exe")], ROOT.parents[2]).stdout)
    expected = {(f"doe.tsir.bootstrap.{kernel}", backend)
                for kernel in ("fused_gemv", "rms_norm", "gather")
                for backend in ("webgpu-generic", "msl", "spir-v")}
    assert len(rows) == 9 and {(row["kernelRef"], row["backend"]) for row in rows} == expected
    inputs = out / "gather.json"
    inputs.write_text(json.dumps({"kernel": "gather", "inputs": {
        "indices": {"elem": "u32", "shape": [1], "values": [0]},
        "table": {"elem": "f32", "shape": [1, 1], "values": [3]},
    }}), encoding="utf-8")
    oracle = json.loads(invoke([str(out / "tsir_bootstrap_oracle.exe"),
                               "--kernel", "gather", "--inputs", str(inputs)]).stdout)
    assert oracle["status"] == "pass"
    assert oracle["referenceHash"] == hashlib.sha256(struct.pack("<f", 3)).hexdigest()
    child = out / "test_dxil_io.exe"
    # The existing controlled child owns fake-artifact production; no duplicate
    # script/compiler fixture is substituted for the shared process boundary.
    invoke([*strict, str(ROOT / "src/dxil/tests/test_dxil_io.c"), *objects,
            "/Fe" + str(child), "/Fo" + str(out / "test_dxil_io.obj"),
            "/link", "/stack:8388608"])
    invoke([str(child)])
    invoke([sys.executable, str(ROOT / "tools/test_host_dxil.py"),
            "--library", str(library), "--child", str(child),
            "--real-dxc", str(args.dxc.resolve())])
    (out / "scope.json").write_text(json.dumps({
        "scope": "Allocating host DLL, ordinary C/C++ consumers and source-to-DXC only; no GPU qualification",
        "library_sha256": hashlib.sha256(library.read_bytes()).hexdigest(),
        "dxc_sha256": hashlib.sha256(args.dxc.read_bytes()).hexdigest(),
        "dxc_dll_sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in sorted(args.dxc.resolve().parent.glob("dx*.dll"))},
        "exports": exports, "source_sha256": identities,
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
