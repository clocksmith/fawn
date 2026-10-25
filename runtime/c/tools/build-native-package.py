#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and relocate a Linux or Darwin C-only package from one committed tree.

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
import re
import shutil
import subprocess
import sys
import tarfile


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def lean_proof_record(source: Path) -> dict[str, object]:
    """Lean proof provenance of the captured tree, from the inputs
    runtime/zig/build.zig reads for verification/lean_proof.zig, hashed the same
    way. No C build step embeds or validates the proof artifact and no C code
    consults Lean proofs, so the build is not Lean-verified and records no proof
    artifact digest (doe-zig-runtime's default -Dlean-verified=false is the same)."""
    lean_root = source / "pipeline/lean/Doe"
    tree = hashlib.sha256()
    for path in sorted(lean_root.rglob("*.lean"), key=lambda p: str(p.relative_to(lean_root)).encode()):
        if not path.is_file():
            continue
        tree.update(f"pipeline/lean/Doe/{path.relative_to(lean_root)}".encode())
        tree.update(b"\n")
        tree.update(path.read_bytes())
        tree.update(b"\n")
    version = json.loads((source / "config/toolchains.json").read_text(encoding="utf-8"))["toolchains"]["lean"]["version"]
    provenance = {
        "leanToolchainRef": "leanprover/lean4:" + (version if version.startswith("v") else "v" + version),
        "extractProgramSha256": digest(lean_root / "Extract.lean"),
        "leanSourceTreeSha256": tree.hexdigest(),
        "generatedComparabilityContractSha256": digest(lean_root / "Generated/ComparabilityContract.lean"),
        "proofPatternSpecSha256": digest(source / "config/lean-proof-patterns.json"),
    }
    artifact = source / "pipeline/lean/artifacts/proven-conditions.json"
    recorded = json.loads(artifact.read_text(encoding="utf-8")).get("provenance", {}) if artifact.is_file() else {}
    return {
        "leanVerifiedBuild": False,
        "proofArtifactSha256": None,
        "provenance": provenance,
        # Whether the tree's proof artifact was extracted from these exact inputs.
        "proofArtifactProvenanceMatches": bool(recorded) and all(recorded.get(k) == v for k, v in provenance.items()),
    }


def execute(arguments: list[str], log: Path, cwd: Path | None = None) -> None:
    with log.open("wb") as output:
        subprocess.run(arguments, cwd=cwd, stdout=output, stderr=subprocess.STDOUT, check=True)


def captured(arguments: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(arguments, cwd=cwd, text=True).strip()


def audit_libraries(directory: Path, system: str) -> dict[str, str]:
    """Refuse build-path dependencies before testing the relocated consumer."""
    if system == "Linux":
        dynamic = captured(["readelf", "-d", str(directory / "libwebgpu_doe_c.so")])
        if "Library runpath: [$ORIGIN]" not in dynamic:
            raise RuntimeError("Native artifact lacks exact $ORIGIN RUNPATH")
        return {"native_dynamic": dynamic}
    records: dict[str, str] = {}
    for name in ("libwebgpu_doe_c.dylib", "libdoe_compiler.dylib"):
        library = directory / name
        identity = captured(["otool", "-D", str(library)]).splitlines()[1:]
        if identity != ["@rpath/" + name]:
            raise RuntimeError(f"{name}: expected @rpath install identity, got {identity}")
        dependencies = captured(["otool", "-L", str(library)])
        links = [line.strip().split(" (", 1)[0]
                 for line in dependencies.splitlines()[1:]]
        expected = {"@rpath/" + name}
        if name == "libwebgpu_doe_c.dylib":
            expected.add("@rpath/libdoe_compiler.dylib")
        actual = {link for link in links if not link.startswith(
            ("/usr/lib/", "/System/Library/"))}
        if actual != expected:
            raise RuntimeError(f"{name}: non-system dependencies {actual}, expected {expected}")
        commands = captured(["otool", "-l", str(library)])
        runpaths = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)",
                             commands)
        if runpaths != (["@loader_path"] if name == "libwebgpu_doe_c.dylib" else []):
            raise RuntimeError(f"{name}: unexpected loader paths {runpaths}")
        records[name] = dependencies + "\n" + commands
    return records


def loader_environment() -> dict[str, str]:
    return {key: value for key, value in os.environ.items()
            if key not in ("LD_LIBRARY_PATH", "LD_PRELOAD")
            and not key.startswith("DYLD_")}


def build_package(repo: Path, revision: str, build: Path, destination: Path,
                  cc: str, cxx: str) -> dict[str, object]:
    system = platform.system()
    if system not in ("Linux", "Darwin"):
        raise ValueError(f"Unsupported package host {system}; use Linux or Darwin")
    if build.exists() or build.is_symlink() or destination.exists() or destination.is_symlink():
        raise FileExistsError("Use fresh build and destination paths; existing work is never replaced")
    build, destination = build.resolve(), destination.resolve()
    if build == destination or build in destination.parents or destination in build.parents:
        raise ValueError("Build and destination must be disjoint directories")
    commit = captured(["git", "rev-parse", "--verify", revision + "^{commit}"], repo)
    tree = captured(["git", "rev-parse", commit + "^{tree}"], repo)
    build.mkdir(parents=True)
    source = build / "source"
    source.mkdir()
    archive = build / "source.tar"
    with archive.open("wb") as output:
        # The compute-program schema supplies the native program contract version;
        # the Lean inputs supply the manifest's lean_proof record.
        subprocess.run(["git", "archive", "--format=tar", commit, "runtime/c",
                        "runtime/zig/vendor/webgpu-headers", "runtime/zig/vendor/spirv-headers/LICENSE", "LICENSE",
                        "config/compute-program.schema.json", "config/toolchains.json",
                        "config/lean-proof-patterns.json", "pipeline/lean/Doe",
                        "pipeline/lean/artifacts/proven-conditions.json"],
                       cwd=repo, stdout=output, check=True)
    with tarfile.open(archive) as bundle:
        bundle.extractall(source, filter="data")
    source_hashes = {str(p.relative_to(source)): digest(p)
                     for p in sorted(source.rglob("*")) if p.is_file()}
    (build / "SOURCE_INPUTS.json").write_text(
        json.dumps(source_hashes, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    owner = source / "runtime/c/native"
    objects = build / "objects"
    # Two dollars survive GNU Make expansion as one literal loader token.
    suffix = "dylib" if system == "Darwin" else "so"
    native_name = "libwebgpu_doe_c." + suffix
    compiler_name = "libdoe_compiler." + suffix
    runpath = "@loader_path" if system == "Darwin" else "$$ORIGIN"
    command = ["make", "-C", str(owner), "all", f"BUILD={objects}",
               f"CC={cc}", f"CXX={cxx}", f"SHARED_PLATFORM={system}",
               f"COMPILER_RUNPATH={runpath}"]
    execute(command, build / "build.log")
    loader_records = audit_libraries(objects, system)
    candidate = build / "package-before-relocation"
    for name in ("lib", "include", "bin", "licenses"):
        (candidate / name).mkdir(parents=True)
    for name in (native_name, compiler_name):
        shutil.copy2(objects / name, candidate / "lib" / name)
    for header in sorted((source / "runtime/c/compiler/include").glob("*.h")):
        shutil.copy2(header, candidate / "include" / header.name)
    for header in sorted((owner / "include").glob("*.h")):  # Doe extension ABI headers.
        shutil.copy2(header, candidate / "include" / header.name)
    vendor = source / "runtime/zig/vendor/webgpu-headers"
    shutil.copy2(vendor / "webgpu.h", candidate / "include/webgpu.h")
    shutil.copy2(vendor / "LICENSE", candidate / "licenses/WebGPU-Headers.txt")
    shutil.copy2(source / "LICENSE", candidate / "licenses/Doe.txt")
    shutil.copy2(source / "runtime/zig/vendor/spirv-headers/LICENSE",
                 candidate / "licenses/Khronos-SPIRV-Headers.txt")
    shutil.copy2(owner / "EXPORTED_SYMBOLS.txt", candidate / "EXPORTED_SYMBOLS.txt")
    shutil.copy2(owner / "EXTENSION_SYMBOLS.txt", candidate / "EXTENSION_SYMBOLS.txt")
    shutil.copy2(owner / "tests/procedures.cpp", candidate / "procedures.cpp")
    consumer_runpath = "@executable_path/../lib" if system == "Darwin" else "$ORIGIN/../lib"
    consumer_command = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        str(candidate / "procedures.cpp"), "-I" + str(candidate / "include"),
                        "-L" + str(candidate / "lib"), "-lwebgpu_doe_c",
                        "-Wl,-rpath," + consumer_runpath, "-o", str(candidate / "bin/procedures")]
    execute(consumer_command, build / "consumer-build.log")
    moved = build / "package-after-relocation"
    candidate.rename(moved)
    environment = loader_environment()
    with (build / "relocated-consumer.log").open("wb") as log:
        subprocess.run([str(moved / "bin/procedures")], cwd="/", env=environment,
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    execute([sys.executable, str(owner / "tests/audit-exports.py"),
             str(moved / "lib" / native_name), "--runtime", "--platform", system],
            build / "exports.log")
    after = {str(p.relative_to(source)): digest(p)
             for p in sorted(source.rglob("*")) if p.is_file()}
    if after != source_hashes:
        raise RuntimeError("Captured source changed during the build")
    artifacts = {str(p.relative_to(moved)): digest(p)
                 for p in sorted(moved.rglob("*")) if p.is_file()}
    manifest: dict[str, object] = {
        "source_commit": commit, "source_tree": tree,
        "source_archive_sha256": digest(archive), "source_inputs": source_hashes,
        "packaging_tool_sha256": digest(Path(__file__)),
        "build_command": command, "consumer_command": consumer_command,
        "cc": captured([cc, "--version"]).splitlines()[0],
        "cxx": captured([cxx, "--version"]).splitlines()[0],
        "host": platform.platform(), "artifacts": artifacts,
        "lean_proof": lean_proof_record(source),
        "loader_records": loader_records,
        "checks": ["native C-only source build", "moved C++ consumer with no library-path override",
                   "exact exported procedure inventory and addresses"],
        "qualification": f"partial {system} developer package; no GPU execution, Windows package, browser or production cutover qualification",
    }
    (moved / "MANIFEST.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
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
    result = build_package(args.repo.resolve(), args.ref, args.build.absolute(),
                           args.destination.absolute(), args.cc, args.cxx)
    print(f"PASS source-bound relocatable partial C package: {result['source_commit']}")
    print(args.destination / "MANIFEST.json")


if __name__ == "__main__":
    main()
