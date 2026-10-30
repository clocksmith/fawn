#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build source-bound SDK/progress controls; physical queue execution is explicit."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parents[2]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-root", type=Path, required=True,
                        help="Existing canonical native Windows build with native-scope.json.")
    parser.add_argument("--compiler-import", type=Path, required=True,
                        help="The exact compiler import library used by that build.")
    parser.add_argument("--clang-cl", required=True, help="Actual Windows SDK clang-cl.")
    parser.add_argument("--out", type=Path, required=True, help="Fresh control directory.")
    parser.add_argument("--run-physical", action="store_true",
                        help="Execute the real queue gate on the selected physical D3D12 adapter.")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("requires actual Windows and SDK declarations; no platform spoofing")
    build = args.build_root.resolve(strict=True)
    imports = args.compiler_import.resolve(strict=True)
    scope = json.loads((build / "native-scope.json").read_text(encoding="utf-8"))
    if digest(imports) != scope["compiler_import_library_sha256"]:
        parser.error("compiler import library differs from the native producer")
    drift = [name for name, expected in scope["input_sha256"].items()
             if digest(REPO / name) != expected]
    if drift:
        parser.error(f"native build/source drift: {drift}")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compiler = shutil.which(args.clang_cl)
    if compiler is None:
        parser.error("explicit clang-cl is unavailable")
    includes = [ROOT / "include", REPO / "runtime/zig/vendor/webgpu-headers",
                REPO / "runtime/c/compiler/include"]
    for owner in scope["owners"]:
        includes.append(REPO / "runtime/c" / owner / "include")
    flags = [compiler, "--target=x86_64-pc-windows-msvc", "/nologo", "/MD", "/O2", "/WX",
             "/D_CRT_SECURE_NO_WARNINGS", "/DWGPU_SHARED_LIBRARY", "/DWGPU_IMPLEMENTATION",
             "/clang:-std=c11", "/clang:-Wall", "/clang:-Wextra", "/clang:-Wpedantic",
             *["/I" + str(path) for path in includes]]
    objects = {name: build / (Path(name).relative_to("runtime/c").as_posix().replace("/", "_") + ".obj")
               for name in scope["sources"]}
    sdk_source = REPO / "runtime/c/backends/d3d12/tests/test_windows_progress.c"
    physical_source = ROOT / "tests/d3d12_async.c"
    zero_source = REPO / "runtime/c/backends/d3d12/tests/test_windows_readback_zero.c"
    reuse_source = REPO / "runtime/c/backends/d3d12/tests/test_windows_command_reuse.c"
    private_objects = [path for name, path in objects.items()
                       if name.startswith("runtime/c/backends/d3d12/")]
    sdk_objects = [path for name, path in objects.items()
                   if name.startswith("runtime/c/backends/d3d12/")
                   and not name.endswith(("/windows_context.c", "/windows_progress.c"))]
    libraries = ["kernel32.lib", "user32.lib", "d3d12.lib", "dxgi.lib", "dxguid.lib"]
    commands = [
        ("sdk-build", flags + [str(sdk_source), "/Fo" + str(out / "sdk.obj"),
                              "/Fe" + str(out / "sdk.exe"), "/link", *map(str, sdk_objects), *libraries]),
        ("sdk-run", [str(out / "sdk.exe")]),
        ("readback-zero-sdk-build", flags + [str(zero_source), "/Fo" + str(out / "zero_sdk.obj"),
                                           "/Fe" + str(out / "zero_sdk.exe"), "/link",
                                           *map(str, private_objects), *libraries]),
        ("readback-zero-sdk-run", [str(out / "zero_sdk.exe")]),
        ("command-reuse-sdk-build", flags + [str(reuse_source), "/Fo" + str(out / "reuse_sdk.obj"),
                                           "/Fe" + str(out / "reuse_sdk.exe"), "/link",
                                           *map(str, private_objects), *libraries]),
        ("command-reuse-sdk-run", [str(out / "reuse_sdk.exe")]),
        ("physical-build", flags + [str(physical_source), "/Fo" + str(out / "physical.obj"),
                                   "/Fe" + str(out / "physical.exe"), "/link", *map(str, objects.values()),
                                   str(build / "d3d12_windows_shader.obj"), str(imports), *libraries]),
    ]
    copied = {}
    for name in ("doe_compiler.dll", "dxcompiler.dll", "dxil.dll", "d3dcompiler_47.dll"):
        shutil.copy2(build / name, out / name)
        copied[name] = digest(out / name)
    if args.run_physical:
        commands.append(("physical-run", [str(out / "physical.exe")]))
        commands.append(("initialization-debug-run", [str(out / "physical.exe"),
                                                       "--initialization-debug"]))
    environment = os.environ.copy()
    environment["DOE_C_TEST_BACKEND"] = "d3d12"
    environment["DOE_C_TEST_ADAPTER"] = "physical"
    rows = []
    for label, command in commands:
        result = subprocess.run(command, cwd=out, env=environment, capture_output=True,
                                text=True, timeout=120)
        (out / (label + ".stdout")).write_text(result.stdout, encoding="utf-8")
        (out / (label + ".stderr")).write_text(result.stderr, encoding="utf-8")
        rows.append({"label": label, "command": command, "exit": result.returncode})
        print(label, result.returncode, result.stdout, result.stderr, flush=True)
        if result.returncode != 0:
            break
    final_drift = [name for name, expected in scope["input_sha256"].items()
                   if digest(REPO / name) != expected]
    receipt = {"scope": "actual SDK call controls; physical execution only with --run-physical",
               "nativeScopeSha256": digest(build / "native-scope.json"),
               "nativeDllSha256": digest(build / "webgpu_doe_c.dll"), "sourceDrift": final_drift,
               "testInputs": {str(path.relative_to(REPO)): digest(path)
                              for path in (sdk_source, zero_source, reuse_source, physical_source, Path(__file__).resolve())},
               "runtimeDlls": copied, "commands": rows}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return 0 if not final_drift and all(row["exit"] == 0 for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main())
