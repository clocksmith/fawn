#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify a pinned checkout and build the independent retained-native test root."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--zig", type=Path, required=True)
    parser.add_argument("--vulkan-lib-dir", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--build-options", type=Path)
    parser.add_argument("--mode", choices=["normal", "asan", "tsan"], default="normal")
    parser.add_argument("--native-library", type=Path)
    parser.add_argument("--build-shared", action="store_true")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    repo, out, zig = args.repo.resolve(), args.out.resolve(), args.zig.resolve()
    options = (args.build_options or here / "build_options.fixture.zig").resolve()
    if not zig.is_file() or not args.vulkan_lib_dir.is_dir():
        raise SystemExit("Provide existing Zig 0.15.2 and Vulkan loader paths")
    expected_options = (here / "BUILD_OPTIONS.sha256").read_text().split()[0]
    if hashlib.sha256(options.read_bytes()).hexdigest() != expected_options:
        raise SystemExit("Selected options do not match this frozen test fixture")
    inputs: list[tuple[Path, str]] = []
    manifest = (here / "SOURCE_INPUTS.sha256").read_text()
    if args.build_shared:
        manifest += (here / "SHARED_SOURCE_INPUTS.sha256").read_text()
    for line in manifest.splitlines():
        expected, name = line.split(maxsplit=1)
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            raise SystemExit(f"Unsafe source input path: {name}")
        path = repo / relative
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise SystemExit(f"Frozen source mismatch or missing input: {name}")
        inputs.append((relative, expected))
    out.mkdir(parents=True, exist_ok=False)
    copied = out / "source"
    for relative, _ in inputs:
        destination = copied / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(repo / relative, destination)
    runtime, futures = copied / "runtime/zig", copied / "runtime/c/futures"
    shutil.copy2(here / "native_cases.zig", runtime / "independent.zig")
    shutil.copy2(options, runtime / "options.zig")
    archive = out / "archive"
    archive.mkdir()
    commands: list[list[str]] = []
    instrumentation = [] if args.mode == "normal" else ["-fno-omit-frame-pointer", "-fno-sanitize-recover=all", "-fsanitize=" + ("address,undefined" if args.mode == "asan" else "thread")]
    for name, source in [("storage", futures / "src/storage.c"), ("futures", futures / "src/futures.c"), ("bridge", futures / "integration/doe_futures_zig_bridge.c")]:
        commands.append(["gcc", "-std=c11", "-O2" if args.mode == "normal" else "-O1", "-g", "-fPIC", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread", *instrumentation, "-I", str(futures / "include"), "-c", str(source), "-o", str(archive / (name + ".o"))])
    commands.append(["ar", "rcs", str(archive / "libdoe_futures_zig.a"), *[str(archive / (name + ".o")) for name in ["storage", "futures", "bridge"]]])
    command = [str(zig), "test", str(archive / "libdoe_futures_zig.a"), str(runtime / "src/backend/d3d12/d3d12_bridge_stubs.c"), "-lvulkan", str(runtime / "src/backend/metal/metal_bridge_stubs.c"), "-OReleaseFast"]
    for directory in [runtime / "src/backend/d3d12", runtime / "src/backend/metal", runtime / "vendor/webgpu-headers", futures / "include", futures / "integration"]:
        command += ["-I", str(directory)]
    command += ["--dep", "build_options", "-Mroot=" + str(runtime / "independent.zig"), "-Mbuild_options=" + str(runtime / "options.zig"), "-lc", "--test-filter", "independent futures:", "--cache-dir", str(out / "cache"), "--global-cache-dir", str(out / "global-cache"), "--name", "independent_tests", "-L", str(args.vulkan_lib_dir.resolve()), "-femit-bin=" + str(out / "independent_tests")]
    if args.mode != "normal":
        sanitizer = "asan" if args.mode == "asan" else "tsan"
        lib = Path(subprocess.check_output(["gcc", f"-print-file-name=lib{sanitizer}.so"], text=True).strip())
        if not lib.is_file():
            raise SystemExit(f"Installed GCC sanitizer runtime missing: {lib}")
        command += ["-L", str(lib.parent), "-l" + sanitizer]
        if args.mode == "asan":
            command += ["-lubsan"]
            command = ["env", "ASAN_OPTIONS=detect_leaks=0", *command]
    commands.append(command)
    if args.build_shared:
        if args.mode != "normal":
            raise SystemExit("This frozen shared-artifact recipe is normal mode only")
        includes = []
        for directory in [runtime / "src/backend/d3d12", runtime / "src/backend/metal", runtime / "vendor/webgpu-headers", futures / "include", futures / "integration"]:
            includes += ["-I", str(directory)]
        commands.append([str(zig), "build-lib", str(archive / "libdoe_futures_zig.a"), str(runtime / "src/backend/d3d12/d3d12_bridge_stubs.c"), "-lvulkan", str(runtime / "src/backend/metal/metal_bridge_stubs.c"), "-OReleaseFast", *includes, "--dep", "build_options", "--dep", "doe", "-Mroot=" + str(runtime / "src/dropin/root.zig"), "-Mbuild_options=" + str(runtime / "options.zig"), "-OReleaseFast", *includes, "--dep", "build_options", "-Mdoe=" + str(runtime / "src/mod.zig"), "-lc", "--cache-dir", str(out / "cache"), "--global-cache-dir", str(out / "global-cache"), "--name", "webgpu_doe", "-dynamic", "-L", str(args.vulkan_lib_dir.resolve()), "-femit-bin=" + str(out / "libwebgpu_doe.so")])
    if args.native_library or args.build_shared:
        library = out / "libwebgpu_doe.so" if args.build_shared else args.native_library.resolve()
        if not args.build_shared and not library.is_file():
            raise SystemExit(f"Native library missing: {library}")
        commands += [["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-I", str(runtime / "vendor/webgpu-headers"), str(here / "native_consumer.c"), "-ldl", "-o", str(out / "native_consumer")], [str(out / "native_consumer"), str(library)], ["g++", "-x", "c++", "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fno-exceptions", "-fno-rtti", "-I", str(runtime / "vendor/webgpu-headers"), str(here / "native_consumer.c"), "-ldl", "-o", str(out / "native_consumer_cpp")], [str(out / "native_consumer_cpp"), str(library)]]
    (out / "plan.json").write_text(json.dumps({"repo": str(repo), "verified_source_files": len(inputs), "build_options_sha256": expected_options, "commands": commands}, indent=2) + "\n")
    for index, command in enumerate(commands, 1):
        print(shlex.join(command), flush=True)
        if not args.prepare_only:
            subprocess.run([sys.executable, str(here / "capture.py"), "--receipt", str(out / f"{index:02d}.json"), "--cwd", str(out), "--timeout", "300", "--", *command], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
