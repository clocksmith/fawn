#!/usr/bin/env python3
"""Build the GCC owner archive or verify its source-bound native admission receipt.

The receipt is evidence of a local build, not an adversarial signing authority.
Build always recompiles both objects, snapshots inputs before and after compiling,
and binds actual archive member bytes. Verification never executes receipt commands.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import time

OWNER = Path("runtime/c/error-scopes")
TARGET = "x86_64-linux-gnu"
FLAGS = ["-std=c11", "-O2", "-g", "-fPIC", "-fno-short-enums", "-fstack-usage",
         "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-pthread"]
SOURCES = {"error_scopes.o": OWNER / "src/error_scopes.c",
           "doe_error_scopes_zig_bridge.o": OWNER / "integration/doe_error_scopes_zig_bridge.c"}
INPUTS = [*SOURCES.values(), OWNER / "src/internal.h", OWNER / "include/doe_error_scopes.h",
          OWNER / "integration/doe_error_scopes_zig_bridge.h", OWNER / "CONTRACT.md",
          OWNER / "CONTRACT.sha256", OWNER / "Makefile", OWNER / "tools/archive.py",
          Path("runtime/zig/vendor/webgpu-headers/webgpu.h"),
          Path("runtime/c/platform/include/doe_platform.h"),
          Path("runtime/c/platform/include/doe_platform_posix.h"),
          Path("runtime/c/platform/include/doe_platform_win32.h")]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def output(command: list[str]) -> str:
    return subprocess.run(command, check=True, capture_output=True, text=True,
                          timeout=30).stdout.strip()


def tool(name: str) -> dict[str, str]:
    found = shutil.which(name)
    if found is None:
        raise ValueError(f"required existing tool not found: {name}")
    path = Path(found).resolve()
    version = output([str(path), "--version"])
    if name == "gcc" and ("gcc" not in version.lower() or "zig" in version.lower()):
        raise ValueError("native error scopes require GCC, not a compiler substitute")
    if name == "ar" and "GNU ar" not in version:
        raise ValueError("native error scopes require GNU ar")
    return {"path": str(path), "sha256": digest(path.read_bytes()), "version": version}


def settings(root: Path) -> dict[str, object]:
    compiler = tool("gcc")
    if output([compiler["path"], "-dumpmachine"]) != TARGET:
        raise ValueError(f"native error scopes require compiler target {TARGET}")
    return {"version": 1, "target": TARGET, "flags": FLAGS,
            "compiler": compiler, "archiver": tool("ar"),
            "sources": {str(path): digest((root / path).read_bytes()) for path in INPUTS}}


def write_record(path: Path, record: dict[str, object]) -> None:
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def build(root: Path, directory: Path) -> Path:
    if not directory.is_absolute():
        raise ValueError("build directory must be absolute")
    directory.mkdir(parents=True, exist_ok=True)
    before = settings(root)
    compiler = before["compiler"]
    archiver = before["archiver"]
    assert isinstance(compiler, dict) and isinstance(archiver, dict)
    includes = ["-I" + str(root / path) for path in
                (OWNER / "include", OWNER / "integration", Path("runtime/zig/vendor/webgpu-headers"))]
    objects: dict[str, str] = {}
    commands = []
    start = time.monotonic()
    for name, source in SOURCES.items():
        command = [compiler["path"], *FLAGS, *includes, "-c", str(root / source),
                   "-o", str(directory / name)]
        subprocess.run(command, check=True)
        commands.append(command)
        objects[name] = digest((directory / name).read_bytes())
    if settings(root) != before:
        raise ValueError("source or tool drift during owner compilation; no receipt published")
    archive = directory / "libdoe_error_scopes.a"
    # A fresh archive cannot inherit unrelated or obsolete members.
    temporary = directory / "libdoe_error_scopes.new.a"
    if temporary.exists():
        raise ValueError(f"unfinished prior archive build retained at {temporary}")
    command = [archiver["path"], "rcs", str(temporary),
               *(str(directory / name) for name in SOURCES)]
    subprocess.run(command, check=True)
    commands.append(command)
    temporary.replace(archive)
    record = {"settings": before, "archive_sha256": digest(archive.read_bytes()),
              "objects": objects, "commands": commands,
              "wall_seconds": time.monotonic() - start}
    write_record(archive.with_suffix(".a.json"), record)
    verify(root, archive)
    return archive


def verify(root: Path, archive: Path) -> None:
    if not archive.is_absolute() or not archive.is_file():
        raise ValueError("archive must be an existing absolute file")
    raw = archive.read_bytes()
    if not raw.startswith(b"!<arch>\n"):
        raise ValueError("expected regular archive, not thin archive or linker script")
    record = json.loads(archive.with_suffix(".a.json").read_text())
    expected = settings(root)
    if record.get("settings") != expected:
        raise ValueError("archive receipt source, tool, target or flag mismatch; rebuild required")
    if record.get("archive_sha256") != digest(raw):
        raise ValueError("archive bytes do not match build receipt")
    archiver = expected["archiver"]
    assert isinstance(archiver, dict)
    members = output([archiver["path"], "t", str(archive)]).splitlines()
    if members != list(SOURCES):
        raise ValueError(f"unexpected archive members: {members}")
    objects = record.get("objects")
    if not isinstance(objects, dict) or set(objects) != set(SOURCES):
        raise ValueError("receipt object set does not match owner")
    for name in SOURCES:
        member = subprocess.run([archiver["path"], "p", str(archive), name],
                                check=True, capture_output=True, timeout=30).stdout
        if (len(member) < 64 or member[:7] != b"\x7fELF\x02\x01\x01" or
                int.from_bytes(member[16:18], "little") != 1 or
                int.from_bytes(member[18:20], "little") != 62):
            raise ValueError(f"{name}: expected x86-64 ELF relocatable member")
        if objects[name] != digest(member):
            raise ValueError(f"{name}: archive member does not match compiled object receipt")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[4])
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--build-directory", type=Path)
    action.add_argument("--archive", type=Path)
    args = parser.parse_args()
    root = args.repo_root.resolve()
    try:
        if args.build_directory is not None:
            archive = build(root, args.build_directory)
            print(f"built and verified {archive}")
        else:
            verify(root, args.archive)
            print(f"verified {args.archive}")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"C error scopes archive rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
