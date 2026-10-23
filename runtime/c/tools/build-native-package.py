#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and relocate a Linux C-only developer package from one committed tree.

This is an explicit partial native provider, not an installer or release gate.
The native Makefile owns compilation; this tool owns immutable source capture,
package contents and a no-device C++ consumer check. Existing paths are refused.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(arguments: list[str], log: Path, cwd: Path | None = None) -> None:
    with log.open("wb") as output:
        subprocess.run(arguments, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, check=True)


def captured(arguments: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(arguments, cwd=cwd, text=True).strip()


def build_package(repo: Path, revision: str, build: Path, destination: Path,
                  cc: str, cxx: str) -> dict[str, object]:
    if platform.system() != "Linux":
        raise ValueError("This package route currently supports Linux ELF only")
    if build.exists() or destination.exists() or destination.is_symlink():
        raise FileExistsError("Use fresh build and destination paths; existing work is never replaced")
    if build == destination or build in destination.parents or destination in build.parents:
        raise ValueError("Build and destination must be disjoint directories")
    commit = captured(["git", "rev-parse", "--verify", revision + "^{commit}"], repo)
    tree = captured(["git", "rev-parse", commit + "^{tree}"], repo)
    build.mkdir(parents=True)
    source = build / "source"
    source.mkdir()
    archive = build / "source.tar"
    with archive.open("wb") as output:
        subprocess.run(["git", "archive", "--format=tar", commit, "runtime/c",
                        "runtime/zig/vendor/webgpu-headers", "runtime/zig/vendor/spirv-headers/LICENSE", "LICENSE"],
                       cwd=repo, stdout=output, check=True)
    with tarfile.open(archive) as bundle:
        bundle.extractall(source, filter="data")
    source_hashes = {str(p.relative_to(source)): digest(p)
                     for p in sorted(source.rglob("*")) if p.is_file()}
    (build / "SOURCE_INPUTS.json").write_text(json.dumps(source_hashes, indent=2) + "\n")
    owner = source / "runtime/c/native"
    objects = build / "objects"
    # Two dollars survive GNU Make expansion as one literal loader token.
    command = ["make", "-C", str(owner), "all", f"BUILD={objects}",
               f"CC={cc}", f"CXX={cxx}", "COMPILER_RUNPATH=$$ORIGIN"]
    execute(command, build / "build.log")
    dynamic = captured(["readelf", "-d", str(objects / "libwebgpu_doe_c.so")])
    if "Library runpath: [$ORIGIN]" not in dynamic:
        raise RuntimeError("Native artifact lacks exact $ORIGIN RUNPATH; do not publish an absolute-path package")
    candidate = build / "package-before-relocation"
    for name in ("lib", "include", "bin", "licenses"):
        (candidate / name).mkdir(parents=True)
    for name in ("libwebgpu_doe_c.so", "libdoe_compiler.so"):
        shutil.copy2(objects / name, candidate / "lib" / name)
    for header in sorted((source / "runtime/c/compiler/include").glob("*.h")):
        shutil.copy2(header, candidate / "include" / header.name)
    vendor = source / "runtime/zig/vendor/webgpu-headers"
    shutil.copy2(vendor / "webgpu.h", candidate / "include/webgpu.h")
    shutil.copy2(vendor / "LICENSE", candidate / "licenses/WebGPU-Headers.txt")
    shutil.copy2(source / "LICENSE", candidate / "licenses/Doe.txt")
    shutil.copy2(source / "runtime/zig/vendor/spirv-headers/LICENSE",
                 candidate / "licenses/Khronos-SPIRV-Headers.txt")
    shutil.copy2(owner / "EXPORTED_SYMBOLS.txt", candidate / "EXPORTED_SYMBOLS.txt")
    shutil.copy2(owner / "tests/procedures.cpp", candidate / "procedures.cpp")
    consumer_command = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        str(candidate / "procedures.cpp"), "-I" + str(candidate / "include"),
                        "-L" + str(candidate / "lib"), "-lwebgpu_doe_c",
                        "-Wl,-rpath,$ORIGIN/../lib", "-o", str(candidate / "bin/procedures")]
    execute(consumer_command, build / "consumer-build.log")
    moved = build / "package-after-relocation"
    candidate.rename(moved)
    environment = os.environ.copy()
    environment.pop("LD_LIBRARY_PATH", None)
    environment.pop("LD_PRELOAD", None)
    with (build / "relocated-consumer.log").open("wb") as log:
        subprocess.run([str(moved / "bin/procedures")], cwd="/", env=environment,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    execute(["python3", str(owner / "tests/audit-exports.py"),
             str(moved / "lib/libwebgpu_doe_c.so"), "--runtime"], build / "exports.log")
    after = {str(p.relative_to(source)): digest(p)
             for p in sorted(source.rglob("*")) if p.is_file()}
    if after != source_hashes:
        raise RuntimeError("Captured source changed during the build")
    artifacts = {str(p.relative_to(moved)): digest(p)
                 for p in sorted(moved.rglob("*")) if p.is_file()}
    manifest: dict[str, object] = {
        "source_commit": commit, "source_tree": tree,
        "source_archive_sha256": digest(archive), "source_inputs": source_hashes,
        "build_command": command, "consumer_command": consumer_command,
        "cc": captured([cc, "--version"]).splitlines()[0],
        "cxx": captured([cxx, "--version"]).splitlines()[0],
        "host": platform.platform(), "artifacts": artifacts,
        "checks": ["native C-only source build", "moved C++ consumer with no library-path override",
                   "exact exported procedure inventory and addresses"],
        "qualification": "partial Linux Vulkan provider; no GPU execution, Windows/macOS package, browser or production cutover qualification",
    }
    (moved / "MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Exclusive creation refuses a concurrent destination. A copy failure leaves
    # a visibly incomplete directory; MANIFEST is copied last as completion marker.
    destination.mkdir(parents=True)
    for child in moved.iterdir():
        if child.name == "MANIFEST.json":
            continue
        target = destination / child.name
        if child.is_dir():
            shutil.copytree(child, target)
        else:
            shutil.copy2(child, target)
    for name, expected in artifacts.items():
        if digest(destination / name) != expected:
            raise RuntimeError(f"Package copy changed artifact {name}")
    shutil.copy2(moved / "MANIFEST.json", destination / "MANIFEST.json")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--ref", default="HEAD")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    result = build_package(args.repo.resolve(), args.ref, args.build.resolve(),
                           args.destination.resolve(), args.cc, args.cxx)
    print(f"PASS source-bound relocatable partial C package: {result['source_commit']}")
    print(args.destination / "MANIFEST.json")


if __name__ == "__main__":
    main()
