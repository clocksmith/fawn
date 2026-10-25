#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and relocate a native C-only package from one committed tree.

This is an explicit partial native provider, not an installer or release gate.
The native build owner compiles; this tool owns immutable source capture,
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
    for path in sorted(lean_root.rglob("*.lean"), key=lambda p: p.relative_to(lean_root).as_posix().encode()):
        if not path.is_file():
            continue
        tree.update(f"pipeline/lean/Doe/{path.relative_to(lean_root).as_posix()}".encode())
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
    environment = os.environ.copy()
    # Build helpers import canonical inventories from the captured source. Python
    # bytecode caches would mutate that immutable input tree and fail its audit.
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    try:
        with log.open("wb") as output:
            subprocess.run(arguments, cwd=cwd, env=environment, stdout=output,
                           stderr=subprocess.STDOUT, check=True)
    except subprocess.CalledProcessError:
        # Preserve the full log, but expose a bounded diagnostic in hosted jobs.
        with log.open("rb") as output:
            output.seek(max(0, log.stat().st_size - 16384))
            tail = output.read().decode("utf-8", errors="replace")
        print(f"Failed child log: {log}\n" + "\n".join(tail.splitlines()[-30:]), flush=True)
        raise


def captured(arguments: list[str], cwd: Path | None = None) -> str:
    return subprocess.check_output(arguments, cwd=cwd, text=True).strip()


def audit_libraries(directory: Path, system: str) -> dict[str, str]:
    """Refuse build-path dependencies before testing the relocated consumer."""
    if system == "Windows":
        records: dict[str, str] = {}
        packaged = {path.name.casefold() for path in directory.glob("*.dll")}
        system_directory = Path(os.environ["SystemRoot"]) / "System32"
        for library in sorted(directory.glob("*.dll")):
            output = captured(["dumpbin", "/nologo", "/dependents", str(library)])
            dependencies = re.findall(r"^\s+([^\s]+\.dll)\s*$", output,
                                      re.MULTILINE | re.IGNORECASE)
            if not dependencies:
                raise RuntimeError(f"No PE dependencies reported for {library.name}")
            for name in dependencies:
                if "/" in name or "\\" in name or ":" in name:
                    raise RuntimeError(f"Nonrelative PE dependency: {name}")
                # API-set names are resolved by Windows, not by PATH. Remaining
                # dependencies must be packaged or actual system DLLs. Never
                # accept an SDK/build directory as a runtime dependency source.
                if (name.casefold() not in packaged
                        and not name.casefold().startswith(("api-ms-win-", "ext-ms-win-"))
                        and not (system_directory / name).is_file()):
                    raise RuntimeError(f"Unpackaged PE dependency: {library.name}: {name}")
            records[library.name] = output
        return records
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


def loader_environment(system: str | None = None) -> dict[str, str]:
    environment = {key: value for key, value in os.environ.items()
            if key not in ("LD_LIBRARY_PATH", "LD_PRELOAD")
            and not key.startswith("DYLD_")}
    if system == "Windows":
        # PE imports first resolve beside the executable. An SDK or build path
        # must not rescue an incomplete relocated package.
        environment = {key: value for key, value in environment.items()
                       if key.casefold() != "path"}
        environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
    return environment


def build_package(repo: Path, revision: str, build: Path, destination: Path,
                  cc: str, cxx: str, *, dxc_root: Path | None = None,
                  clang_cl: str = "clang-cl") -> dict[str, object]:
    system = platform.system()
    if system not in ("Linux", "Darwin", "Windows"):
        raise ValueError(f"Unsupported package host {system}")
    if system == "Windows":
        if dxc_root is None or not (dxc_root / "inc/dxcapi.h").is_file():
            raise ValueError("Windows requires --dxc-root with the official DXC SDK")
        cc = cxx = clang_cl
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
                        # The Windows compiler builder executes its ordinary
                        # native consumer against this retained shader fixture.
                        "bench/inference-pipeline/kernels/residual.wgsl",
                        "config/compute-program.schema.json", "config/toolchains.json",
                        "config/lean-proof-patterns.json", "pipeline/lean/Doe",
                        "pipeline/lean/artifacts/proven-conditions.json"],
                       cwd=repo, stdout=output, check=True)
    with tarfile.open(archive) as bundle:
        bundle.extractall(source, filter="data")
    source_hashes = {p.relative_to(source).as_posix(): digest(p)
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
    if system == "Windows":
        native_name, compiler_name = "webgpu_doe_c.dll", "doe_compiler.dll"
        command = [sys.executable, str(owner / "tools/build_windows.py"),
                   "--out", str(objects), "--dxc-root", str(dxc_root.resolve()),
                   "--clang-cl", clang_cl]
    else:
        command = ["make", "-C", str(owner), "all", f"BUILD={objects}",
                   f"CC={cc}", f"CXX={cxx}", f"SHARED_PLATFORM={system}",
                   f"COMPILER_RUNPATH={runpath}"]
    execute(command, build / "build.log")
    loader_records = audit_libraries(objects, system)
    candidate = build / "package-before-relocation"
    for name in ("lib", "include", "bin", "licenses"):
        (candidate / name).mkdir(parents=True)
    runtime_directory = "bin" if system == "Windows" else "lib"
    for name in (native_name, compiler_name):
        shutil.copy2(objects / name, candidate / runtime_directory / name)
    if system == "Windows":
        for name in ("dxcompiler.dll", "dxil.dll"):
            shutil.copy2(objects / name, candidate / "bin" / name)
        shutil.copy2(objects / "webgpu_doe_c.lib", candidate / "lib/webgpu_doe_c.lib")
        shutil.copy2(objects / "compiler/doe_compiler.lib", candidate / "lib/doe_compiler.lib")
        shutil.copytree(objects / "dxc-licenses", candidate / "licenses/DXC")
        shutil.copy2(objects / "native-scope.json", candidate / "NATIVE_BUILD.json")
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
    consumer_name = "procedures.exe" if system == "Windows" else "procedures"
    consumer_command = [cxx, "-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                        str(candidate / "procedures.cpp"), "-I" + str(candidate / "include"),
                        "-L" + str(candidate / "lib"), "-lwebgpu_doe_c",
                        "-Wl,-rpath," + consumer_runpath, "-o", str(candidate / "bin/procedures")]
    if system == "Windows":
        consumer_command = [clang_cl, "--target=x86_64-pc-windows-msvc", "/nologo",
                            "/MD", "/O2", "/WX", "/std:c++17", "/DWGPU_SHARED_LIBRARY",
                            "/clang:-Wall", "/clang:-Wextra", "/clang:-Wpedantic",
                            str(candidate / "procedures.cpp"),
                            "/I" + str(candidate / "include"),
                            "/Fo" + str(build / "procedures.obj"),
                            "/Fe" + str(candidate / "bin" / consumer_name),
                            "/link", str(candidate / "lib/webgpu_doe_c.lib")]
    execute(consumer_command, build / "consumer-build.log")
    moved = build / "package-after-relocation"
    candidate.rename(moved)
    environment = loader_environment(system)
    working_directory = build / "empty-working-directory"
    working_directory.mkdir()
    with (build / "relocated-consumer.log").open("wb") as log:
        subprocess.run([str(moved / "bin" / consumer_name)], cwd=working_directory,
                       env=environment, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=30)
    execute([sys.executable, str(owner / "tests/audit-exports.py"),
             str(moved / runtime_directory / native_name), "--runtime", "--platform", system],
            build / "exports.log")
    after = {p.relative_to(source).as_posix(): digest(p)
             for p in sorted(source.rglob("*")) if p.is_file()}
    if after != source_hashes:
        changed = sorted(name for name in source_hashes.keys() | after.keys()
                         if source_hashes.get(name) != after.get(name))
        raise RuntimeError(f"Captured source changed during the build: {changed[:8]}")
    artifacts = {p.relative_to(moved).as_posix(): digest(p)
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
        "qualification": f"partial {system} developer package; no GPU execution, browser or production cutover qualification",
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
    parser.add_argument("--dxc-root", type=Path,
                        help="official Windows DXC SDK with headers, x64 DLLs and licenses")
    parser.add_argument("--clang-cl", default="clang-cl",
                        help="Windows SDK clang-cl compiler (Windows only)")
    args = parser.parse_args()
    result = build_package(args.repo.resolve(), args.ref, args.build.absolute(),
                           args.destination.absolute(), args.cc, args.cxx,
                           dxc_root=args.dxc_root, clang_cl=args.clang_cl)
    print(f"PASS source-bound relocatable partial C package: {result['source_commit']}")
    print(args.destination / "MANIFEST.json")


if __name__ == "__main__":
    main()
