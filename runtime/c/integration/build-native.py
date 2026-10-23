#!/usr/bin/env python3
"""Build the explicit Linux hybrid selection, then run CPU-only DSO consumers.

Existing GCC and pinned Zig only. Hardware execution is a separate, explicit
invocation of native-smoke-c or native-smoke-cpp with the hardware argument.
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
import time

OWNERS = ("futures", "callbacks", "instance", "labels", "error-scopes", "dropin-policy")
ARCHIVES = ("libdoe_futures_zig.a", "libdoe_callbacks.a", "libdoe_instance.a",
            "libdoe_labels_native.a", "libdoe_error_scopes.a", "libdoe_dropin_policy.a")
PRIVATE_BRIDGES = (
    "doe_futures_zig_queue_layout", "doe_futures_zig_completion_layout",
    "doe_futures_zig_queue_init", "doe_futures_zig_completion_init",
    "doe_error_scopes_zig_stack_layout", "doe_error_scopes_zig_entry_layout",
    "doe_error_scopes_zig_result_layout", "doe_error_scopes_zig_string_layout",
    "doe_error_scopes_zig_init",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(root: Path) -> dict[str, str]:
    paths = {root / "runtime/zig/build.zig", root / "runtime/zig/source-layout.json"}
    directories = [root / "runtime/zig/src", root / "runtime/zig/vendor/webgpu-headers",
                   root / "config", root / "pipeline/lean", root / "runtime/c/integration"]
    directories.extend(root / "runtime/c" / owner for owner in OWNERS)
    for directory in directories:
        paths.update(path for path in directory.rglob("*") if path.is_file()
                     and "build" not in path.relative_to(directory).parts
                     and (path.suffix in {".c", ".h", ".zig", ".json", ".lean", ".py", ".sh", ".map"}
                          or path.name == "Makefile"))
    return {str(path.relative_to(root)): digest(path) for path in sorted(paths)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="New absolute build/output directory")
    parser.add_argument("--zig", type=Path, required=True, help="Existing Zig 0.15.2 executable")
    parser.add_argument("--vulkan-prefix", type=Path, help="Existing prefix containing lib/libvulkan.so")
    parser.add_argument("--optimize", choices=("Debug", "ReleaseFast"), default="ReleaseFast",
                        help="Retained native optimization mode")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[3]
    out = args.output
    if not out.is_absolute() or out.exists():
        parser.error("--output must be a new absolute directory; prior evidence is not overwritten")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("selected owners are currently admitted only for native Linux x86-64 GNU ABI")
    zig = args.zig.resolve(strict=True)
    if subprocess.check_output([str(zig), "version"], text=True).strip() != "0.15.2":
        parser.error("use the already installed repository-pinned Zig 0.15.2")
    if subprocess.check_output(["gcc", "-dumpmachine"], text=True).strip() != "x86_64-linux-gnu":
        parser.error("existing native GCC x86_64-linux-gnu is required")
    for tool in ("gcc", "g++", "ar", "make", "readelf"):
        if shutil.which(tool) is None:
            parser.error(f"required existing tool unavailable: {tool}")
    out.mkdir(parents=True)
    before = inputs(root)
    records: list[dict[str, object]] = []
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")

    def run(name: str, command: list[str], cwd: Path = root,
            env: dict[str, str] | None = None) -> None:
        start = time.monotonic()
        with (out / (name + ".log")).open("w", encoding="utf-8") as log:
            completed = subprocess.run(command, cwd=cwd, env=env or environment,
                                       stdout=log, stderr=subprocess.STDOUT)
        records.append({"name": name, "command": command, "cwd": str(cwd),
                        "seconds": time.monotonic() - start, "exit_code": completed.returncode})
        (out / "commands.json").write_text(json.dumps(records, indent=2, sort_keys=True) + "\n",
                                          encoding="utf-8")
        if completed.returncode != 0:
            raise RuntimeError(f"{name} failed ({completed.returncode}); retained log: {out / (name + '.log')}")

    for owner in OWNERS[:3]:
        run(owner, ["sh", str(root / "runtime/c" / owner / "integration/build_archive.sh"), str(out / owner)])
    for owner in OWNERS[3:]:
        command = ["make", "-C", str(root / "runtime/c" / owner), "BUILD=" + str(out / owner)]
        if owner == "error-scopes":
            command.append("native-archive")
        run(owner, command)
    command = [str(zig), "build", "dropin", "-j1", "-Doptimize=" + args.optimize]
    command.extend("-Dexperimental-c-" + owner + "-archive=" + str(out / owner / archive)
                   for owner, archive in zip(OWNERS, ARCHIVES))
    command.extend(["--cache-dir", str(out / "cache"), "--global-cache-dir", str(out / "global-cache"),
                    "--prefix", str(out / "install")])
    if args.vulkan_prefix is not None:
        command.extend(["--search-prefix", str(args.vulkan_prefix.resolve(strict=True))])
    run("native-build", command, root / "runtime/zig")
    library = out / "install/lib/libwebgpu_doe.so"
    integration = root / "runtime/c/integration"
    common = ["-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-I",
              str(root / "runtime/zig/vendor/webgpu-headers")]
    run("consumer-c-build", ["gcc", "-std=c11", *common, str(integration / "native_smoke.c"),
                             "-ldl", "-o", str(out / "native-smoke-c")])
    run("consumer-cpp-build", ["g++", "-x", "c++", "-std=c++17", "-fno-exceptions", "-fno-rtti",
                               *common, str(integration / "native_smoke.c"), "-ldl", "-o", str(out / "native-smoke-cpp")])
    run("preload-build", ["gcc", "-std=c11", *common, "-shared", "-fPIC",
                          str(integration / "bridge_preemption.c"), "-o", str(out / "bridge-preemption.so")])
    symbols = subprocess.check_output(["readelf", "--dyn-syms", "--wide", str(library)], text=True)
    exported = {line.split()[-1] for line in symbols.splitlines() if len(line.split()) >= 8}
    leaked = sorted(exported.intersection(PRIVATE_BRIDGES))
    dynamic = subprocess.check_output(["readelf", "-d", str(library)], text=True)
    if leaked or "libvulkan.so.1" not in dynamic:
        raise RuntimeError(f"selected ELF has private bridge leak {leaked} or no real Vulkan loader dependency")
    (out / "elf-dynamic.txt").write_text(dynamic, encoding="utf-8")
    (out / "elf-symbols.txt").write_text(symbols, encoding="utf-8")
    cpu_environment = dict(environment, DOE_BACKEND="vulkan", FAWN_BACKEND_LANE="vulkan_doe_app",
                           VK_ICD_FILENAMES=str(out / "deliberately-absent-icd.json"),
                           VK_DRIVER_FILES=str(out / "deliberately-absent-icd.json"))
    cpu_environment.pop("LD_PRELOAD", None)
    for language in ("c", "cpp"):
        command = [str(out / ("native-smoke-" + language)), str(library), "no-adapter"]
        run("no-adapter-" + language, command, env=cpu_environment)
        run("private-preemption-" + language, command,
            env=dict(cpu_environment, LD_PRELOAD=str(out / "bridge-preemption.so")))
    after = inputs(root)
    if before != after:
        raise RuntimeError("source changed during this build; artifacts retained but no source-bound success receipt")
    receipt = {"scope": "Linux x86-64 hybrid build and real Vulkan loader/no-adapter only",
               "hardware_executed": False, "production_default_changed": False,
               "selected_owners": list(OWNERS), "retained_backend": "Zig Vulkan",
               "private_bridge_exports": leaked, "artifact": str(library),
               "artifact_sha256": digest(library), "sources": before,
               "commands": records}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"PASS: hybrid build and CPU-only loader checks; receipt {out / 'receipt.json'}")
    print("Hardware handoff only: DOE_BACKEND=vulkan FAWN_BACKEND_LANE=vulkan_doe_app " + str(out / "native-smoke-c") + " " + str(library) + " hardware")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
