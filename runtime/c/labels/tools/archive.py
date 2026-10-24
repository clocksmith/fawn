#!/usr/bin/env python3
"""Build and verify source-bound normal-profile label archives with existing GCC.

The producer compiles objects itself, then binds source, tool, command and archive
bytes in one receipt. It never retrospectively blesses supplied object files.
Receipts detect stale or mismatched artifacts; they are not cryptographic signing.
Verification executes only locally constructed tool commands, never receipt text.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import time

OWNER = Path("runtime/c/labels")
TARGET = "x86_64-linux-gnu"
FLAGS = ["-std=c11", "-O2", "-g", "-fPIC", "-fvisibility=hidden", "-pthread",
         "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion", "-Wshadow",
         "-Wstrict-prototypes", "-Wmissing-prototypes"]
ARCHIVES = {"libdoe_labels.a": ["store"],
            "libdoe_labels_native.a": ["store", "native"]}
SYMBOLS = {
    "store": {"doe_label_store_create", "doe_label_store_set",
              "doe_label_store_get", "doe_label_store_remove",
              "doe_label_store_destroy", "doe_labels_destroy_contents",
              "doe_labels_fatal"},
    "native": {"doe_labels_native_set", "doe_labels_native_get",
               "doe_labels_native_remove"},
}


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            hasher.update(block)
    return hasher.hexdigest()


def run(command: list[str], root: Path) -> bytes:
    return subprocess.run(command, cwd=root, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=60).stdout


def executable(name: str) -> Path:
    found = shutil.which(name)
    if found is None:
        raise ValueError(f"required existing tool is unavailable: {name}")
    return Path(found).resolve(strict=True)


def tool_record(path: Path, root: Path) -> dict[str, str]:
    return {"path": str(path), "sha256": digest(path),
            "version": run([str(path), "--version"], root).decode().strip()}


def inputs(root: Path, compiler: str, archiver: str) -> dict[str, object]:
    cc = executable(compiler)
    ar = executable(archiver)
    if run([str(cc), "-dumpmachine"], root).decode().strip() != TARGET:
        raise ValueError("normal label archives require GCC x86_64-linux-gnu")
    cc_record = tool_record(cc, root)
    if "gcc" not in cc_record["version"].lower():
        raise ValueError("normal label archives require GCC")
    tools = {"cc": cc_record, "ar": tool_record(ar, root)}
    if "GNU ar" not in tools["ar"]["version"]:
        raise ValueError("normal label archives require GNU ar")
    for program in ("cc1", "as", "collect2", "ld"):
        location = run([str(cc), "-print-prog-name=" + program], root).decode().strip()
        tools[program] = tool_record(executable(location), root)
    for program in ("readelf", "nm"):
        tools[program] = tool_record(executable(program), root)
    paths = {OWNER / "Makefile", OWNER / "tools/archive.py",
             OWNER / "CONTRACT.md", OWNER / "CONTRACT.sha256",
             OWNER / "integration/CONTRACT.md"}
    paths.update(path.relative_to(root) for directory in ("src", "include")
                 for path in (root / OWNER / directory).rglob("*") if path.is_file())
    paths.update(path.relative_to(root) for path in
                 (root / "runtime/c/platform/include").glob("*.h"))
    return {"format_version": 1, "target": TARGET, "profile": "normal",
            "root": str(root), "flags": FLAGS, "tools": tools,
            "sources": {str(path): digest(root / path) for path in sorted(paths)}}


def command_for(root: Path, build: Path, name: str,
                settings: dict[str, object]) -> list[str]:
    return [settings["tools"]["cc"]["path"], *FLAGS, "-I",
            str(root / OWNER / "include"), "-MD", "-MF",
            str(build / (name + ".d")), "-c", str(root / OWNER / "src" / (name + ".c")),
            "-o", str(build / (name + ".o"))]


def dependency_hashes(text: str) -> dict[str, str]:
    text = text.replace("\\\n", " ")
    dependencies = shlex.split(text.split(":", 1)[1])
    return {str(Path(item).resolve(strict=True)): digest(Path(item))
            for item in sorted(set(dependencies))}


def check_object(path: Path, name: str, root: Path,
                 settings: dict[str, object]) -> None:
    data = path.read_bytes()
    if (len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01" or
            int.from_bytes(data[16:18], "little") != 1 or
            int.from_bytes(data[18:20], "little") != 62):
        raise ValueError(f"{path}: expected ELF64 x86-64 relocatable object")
    readelf = settings["tools"]["readelf"]["path"]
    symbols = run([readelf, "--wide", "--syms", str(path)], root).decode()
    defined = set()
    undefined = set()
    for line in symbols.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].endswith(":"):
            continue
        if fields[4] in ("GLOBAL", "WEAK") and fields[6] != "UND":
            if fields[4] != "GLOBAL" or fields[5] != "HIDDEN":
                raise ValueError(f"{path}: preemptible or weak symbol: {fields[7]}")
            defined.add(fields[7])
        if fields[6] == "UND":
            undefined.add(fields[7])
        if fields[6] == "UND" and ("doe_labels_eval" in fields[7] or
                                     "__asan" in fields[7] or "__tsan" in fields[7]):
            raise ValueError(f"{path}: instrumented import in normal profile")
    if defined != SYMBOLS[name]:
        raise ValueError(f"{path}: wrong owner symbol set: {sorted(defined)}")
    # Absolute narrow relocations in allocated sections indicate non-PIC input.
    # Debug sections legitimately contain these relocations and are excluded.
    relocations = run([readelf, "--wide", "--relocs", str(path)], root).decode()
    section = ""
    for line in relocations.splitlines():
        if line.startswith("Relocation section '"):
            section = line.split("'")[1]
        if ".debug" not in section:
            fields = line.split()
            if any(field in {"R_X86_64_32", "R_X86_64_32S"} for field in fields):
                raise ValueError(f"{path}: non-PIC relocation in {section}")
            if (len(fields) >= 5 and fields[2] == "R_X86_64_PC32" and
                    fields[4] in undefined):
                raise ValueError(f"{path}: non-PIC external PC32 relocation to {fields[4]}")


def write_json(path: Path, value: object) -> None:
    data = json.dumps(value, indent=2, sort_keys=True) + "\n"
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(data, encoding="utf-8")
    temporary.replace(path)


def read_json(path: Path) -> dict[str, object]:
    def unique(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate receipt key: {key}")
            result[key] = value
        return result
    value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected a receipt object")
    return value


def verify(archive: Path, root: Path, settings: dict[str, object]) -> None:
    if archive.name not in ARCHIVES or not archive.is_absolute():
        raise ValueError("expected an absolute normal-profile label archive path")
    receipt = read_json(Path(str(archive) + ".receipt.json"))
    if set(receipt) != {"settings", "archive", "objects", "archive_command", "build_seconds"}:
        raise ValueError("unexpected or missing receipt fields")
    if receipt["settings"] != settings:
        raise ValueError("stale source, header, producer, target, profile, flags or tools")
    if receipt["archive"] != {"path": str(archive), "sha256": digest(archive)}:
        raise ValueError("archive identity mismatch")
    if archive.read_bytes()[:8] != b"!<arch>\n":
        raise ValueError("only regular archives are admitted")
    ar = settings["tools"]["ar"]["path"]
    names = ARCHIVES[archive.name]
    members = run([ar, "t", str(archive)], root).decode().splitlines()
    if members != [name + ".o" for name in names]:
        raise ValueError("missing, extra, duplicate or reordered archive members")
    if set(receipt["objects"]) != set(names):
        raise ValueError("wrong object provenance inventory")
    expected_ar = [ar, "rcsD", str(archive)] + [str(archive.parent / (name + ".o"))
                                             for name in names]
    if receipt["archive_command"] != expected_ar:
        raise ValueError("wrong archive command")
    for name in names:
        record = receipt["objects"][name]
        if set(record) != {"sha256", "command", "dependencies"}:
            raise ValueError(f"wrong object provenance fields: {name}")
        member = run([ar, "p", str(archive), name + ".o"], root)
        if hashlib.sha256(member).hexdigest() != record["sha256"]:
            raise ValueError(f"archive member identity mismatch: {name}")
        if record["command"] != command_for(root, archive.parent, name, settings):
            raise ValueError(f"wrong compile command: {name}")
        dependencies = record["dependencies"]
        if not isinstance(dependencies, dict) or not dependencies:
            raise ValueError(f"missing dependency inventory: {name}")
        preprocess = [settings["tools"]["cc"]["path"], *FLAGS, "-I",
                      str(root / OWNER / "include"), "-M",
                      str(root / OWNER / "src" / (name + ".c"))]
        current_dependencies = dependency_hashes(run(preprocess, root).decode())
        if dependencies != current_dependencies:
            raise ValueError(f"missing, extra or stale compile dependency: {name}")
        # Validate symbol/PIC properties from the actual archived bytes, not a
        # neighboring object that could differ. The scratch file is removed.
        temporary = archive.parent / (".verify-" + name + ".o")
        try:
            temporary.write_bytes(member)
            check_object(temporary, name, root, settings)
        finally:
            temporary.unlink(missing_ok=True)


def build_archives(build: Path, root: Path, settings: dict[str, object]) -> None:
    build.mkdir(parents=True, exist_ok=True)
    if all((build / name).is_file() for name in ARCHIVES):
        try:
            for name in ARCHIVES:
                verify(build / name, root, settings)
            print("label archives unchanged; identities verified")
            return
        except (OSError, ValueError, KeyError, TypeError):
            pass  # A stale local build must rebuild, never admit the old archive.
    started = time.monotonic()
    objects = {}
    for name in ("store", "native"):
        command = command_for(root, build, name, settings)
        run(command, root)
        check_object(build / (name + ".o"), name, root, settings)
        objects[name] = {"sha256": digest(build / (name + ".o")),
                         "command": command,
                         "dependencies": dependency_hashes((build / (name + ".d")).read_text(encoding="utf-8"))}
    # Catch source/tool drift during compilation before publishing any receipt.
    if inputs(root, settings["tools"]["cc"]["path"],
              settings["tools"]["ar"]["path"]) != settings:
        raise ValueError("source or tool changed during the build")
    for filename, names in ARCHIVES.items():
        archive = build / filename
        archive.unlink(missing_ok=True)
        command = [settings["tools"]["ar"]["path"], "rcsD", str(archive)]
        command.extend(str(build / (name + ".o")) for name in names)
        run(command, root)
        write_json(Path(str(archive) + ".receipt.json"), {
            "settings": settings,
            "archive": {"path": str(archive), "sha256": digest(archive)},
            "objects": {name: objects[name] for name in names},
            "archive_command": command,
            "build_seconds": time.monotonic() - started,
        })
        verify(archive, root, settings)
        print(f"verified {archive}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", required=True, type=Path, help="source checkout")
    parser.add_argument("--compiler", default="gcc", help="existing GCC executable")
    parser.add_argument("--archiver", default="ar", help="existing GNU ar executable")
    parser.add_argument("--target", default=TARGET, help="required admitted target")
    parser.add_argument("--profile", default="normal", help="required normal profile")
    parser.add_argument("--require-native", action="store_true", help="require the private native singleton archive")
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--build-dir", type=Path, help="build both normal archives here")
    operation.add_argument("--archive", type=Path, help="verify one existing absolute archive")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.target != TARGET or args.profile != "normal":
            raise ValueError("only normal Linux x86-64 archives are admitted")
        if args.require_native and (args.archive is None or args.archive.name != "libdoe_labels_native.a"):
            raise ValueError("native selection requires libdoe_labels_native.a")
        root = args.repo_root.resolve(strict=True)
        settings = inputs(root, args.compiler, args.archiver)
        if args.build_dir is not None:
            build_archives(args.build_dir.resolve(), root, settings)
        else:
            verify(args.archive, root, settings)
            print(f"verified {args.archive}")
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"label archive rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
