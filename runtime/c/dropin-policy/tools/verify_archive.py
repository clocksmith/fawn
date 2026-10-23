#!/usr/bin/env python3
"""Build receipts and fail-closed freshness checks for the GCC-only policy archive.

Receipts bind bytes and build inputs, not an adversarial signing authority. Each
object has its own post-compilation receipt: recording an archive cannot bless
an old object against newly captured settings. Verification never executes a
compiler command taken from an external receipt.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

TARGET = "x86_64-linux-gnu"
PROFILE = 1
OWNER = Path("runtime/c/dropin-policy")
FLAGS = ("-std=c11", "-O2", "-fPIC", "-fvisibility=hidden", "-pthread",
         "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Wconversion",
         "-Wshadow", "-Wstrict-prototypes", "-Wmissing-prototypes")
CONFIG_FILES = (
    "config/dropin-abi-behavior.json",
    "config/dropin-symbol-ownership.json",
    "config/dropin-abi-behavior.schema.json",
    "config/dropin-symbol-ownership.schema.json",
)
REQUIRED_FILES = (
    *CONFIG_FILES,
    str(OWNER / "Makefile"),
    str(OWNER / "tools/generate_config.py"),
    str(OWNER / "tools/verify_archive.py"),
    str(OWNER / "include/doe_dropin_policy.h"),
    str(OWNER / "include/doe_dropin_policy_private.h"),
    str(OWNER / "src/policy.c"),
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def hash_file(path: Path, *, input_limit: bool = False) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        if input_limit:
            raw = stream.read(65537)
            if len(raw) > 65536:
                raise ValueError(f"{path}: exceeds 65536-byte input limit")
            digest.update(raw)
        else:
            while block := stream.read(65536):
                digest.update(block)
    return digest.hexdigest()


def canonical(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, indent=2) + "\n").encode("utf-8")


def atomic_write(path: Path, data: bytes) -> None:
    if path.is_file() and path.read_bytes() == data:
        return
    temporary: str | None = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".",
                                         delete=False) as stream:
            temporary = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        temporary = None
    finally:
        if temporary is not None:
            Path(temporary).unlink(missing_ok=True)


def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise ValueError(f"duplicate receipt key: {key}")
        value[key] = item
    return value


def read_record(path: Path) -> dict[str, object]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream, object_pairs_hook=unique_object)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: expected record object")
    return value


def source_hashes(root: Path) -> dict[str, str]:
    paths = set(REQUIRED_FILES)
    for directory in (OWNER / "src", OWNER / "include"):
        for path in (root / directory).rglob("*"):
            if path.is_file() and path.suffix in (".c", ".h"):
                paths.add(path.relative_to(root).as_posix())
    return {name: hash_file(root / name, input_limit=name in CONFIG_FILES[:2])
            for name in sorted(paths)}


def run_output(command: list[str]) -> str:
    completed = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, timeout=30)
    return completed.stdout.strip()


def tool_identity(command: str, *, compiler: bool) -> dict[str, str]:
    executable = shutil.which(command)
    if executable is None:
        raise ValueError(f"tool not found: {command}")
    path = Path(executable).resolve()
    version = run_output([str(path), "--version"])
    if compiler:
        if "gcc" not in version.lower() or "zig" in version.lower():
            raise ValueError("the policy archive requires the existing GCC compiler")
        target = run_output([str(path), "-dumpmachine"])
        if target != TARGET:
            raise ValueError(f"unsupported actual compiler target: {target}")
    else:
        if "GNU ar" not in version:
            raise ValueError("the policy archive requires existing GNU ar")
        target = ""
    return {"path": str(path), "sha256": hash_file(path),
            "version": version, "target": target}


def build_settings(root: Path, compiler: str, archiver: str,
                   target: str, profile: int) -> dict[str, object]:
    if target != TARGET:
        raise ValueError(f"unsupported target: {target}; expected {TARGET}")
    if profile != PROFILE:
        raise ValueError(f"unsupported admission profile: {profile}")
    return {"format_version": 1, "target": target, "profile": profile,
            "dependencies": source_hashes(root), "mandatory_c_flags": list(FLAGS),
            "compiler": tool_identity(compiler, compiler=True),
            "archiver": tool_identity(archiver, compiler=False)}


def compare_settings(actual: dict[str, object],
                     expected: dict[str, object]) -> None:
    actual_dependencies = actual.get("dependencies")
    expected_dependencies = expected["dependencies"]
    if not isinstance(actual_dependencies, dict):
        raise ValueError("receipt is missing dependency hashes")
    assert isinstance(expected_dependencies, dict)
    for name in sorted(set(actual_dependencies) | set(expected_dependencies)):
        if actual_dependencies.get(name) != expected_dependencies.get(name):
            raise ValueError(f"stale dependency: {name}")
    for key in expected:
        if actual.get(key) != expected[key]:
            raise ValueError(f"mismatched build setting: {key}")
    if set(actual) != set(expected):
        raise ValueError("unexpected build settings fields")


def checked_settings(args: argparse.Namespace) -> dict[str, object]:
    if args.settings is None:
        raise ValueError("recording requires --settings")
    actual = read_record(args.settings)
    expected = build_settings(args.repo_root, args.compiler, args.archiver,
                              args.target, args.profile)
    compare_settings(actual, expected)
    return actual


def check_elf(data: bytes, label: str) -> None:
    # ELF64 little-endian, current ELF version, ET_REL and EM_X86_64.
    if (len(data) < 64 or data[:7] != b"\x7fELF\x02\x01\x01" or
            int.from_bytes(data[16:18], "little") != 1 or
            int.from_bytes(data[18:20], "little") != 62):
        raise ValueError(f"{label}: not an x86_64 ELF64 relocatable object")


def archive_members(raw: bytes) -> dict[str, bytes]:
    if raw[:8] != b"!<arch>\n":
        raise ValueError("not a regular static archive (thin archives are forbidden)")
    members: dict[str, bytes] = {}
    names = b""
    offset = 8
    while offset < len(raw):
        header = raw[offset:offset + 60]
        if len(header) != 60 or header[58:] != b"`\n":
            raise ValueError("invalid archive member header")
        try:
            size = int(header[48:58].decode("ascii").strip())
            name = header[:16].decode("ascii").rstrip()
        except (UnicodeError, ValueError) as exc:
            raise ValueError("invalid archive member name/size") from exc
        if size < 0 or size > len(raw) - offset - 60:
            raise ValueError("archive member exceeds archive extent")
        start = offset + 60
        data = raw[start:start + size]
        offset = start + size + (size % 2)
        if offset > len(raw):
            raise ValueError("missing archive member padding")
        if name == "//":
            names = data
            continue
        if name in ("/", "/SYM64/"):
            continue
        if name.startswith("/"):
            try:
                name_offset = int(name[1:])
            except ValueError as exc:
                raise ValueError("invalid GNU archive name reference") from exc
            end = names.find(b"/\n", name_offset)
            if name_offset < 0 or end < 0:
                raise ValueError("invalid GNU archive name table")
            name = names[name_offset:end].decode("ascii")
        else:
            name = name.removesuffix("/")
        if name in members:
            raise ValueError(f"duplicate archive member: {name}")
        check_elf(data, name)
        members[name] = data
    return members


def compile_command(args: argparse.Namespace, settings: dict[str, object],
                    source: Path, output: Path) -> list[str]:
    compiler = settings["compiler"]
    assert isinstance(compiler, dict)
    return [str(compiler["path"]), *FLAGS, "-I",
            str(args.repo_root / OWNER / "include"), "-c", str(source),
            "-o", str(output)]


def object_record(args: argparse.Namespace, settings: dict[str, object],
                  source: Path, output: Path) -> dict[str, object]:
    check_elf(output.read_bytes(), output.name)
    return {"format_version": 1, "name": output.name,
            "sha256": sha256(output.read_bytes()),
            "settings_sha256": sha256(canonical(settings)),
            "source": str(source), "source_sha256": sha256(source.read_bytes()),
            "compile_command": compile_command(args, settings, source, output)}


def receipt_path(archive: Path) -> Path:
    return archive.with_name(archive.name + ".receipt.json")


def expected_objects(root: Path, directory: Path) -> list[tuple[Path, Path]]:
    # This intentionally matches the bounded Make compilation, not a framework.
    return [(root / OWNER / "src/policy.c", directory / "policy.o"),
            (directory / "generated_config.c", directory / "generated_config.o")]


def record_archive(args: argparse.Namespace, settings: dict[str, object]) -> None:
    archive = args.archive
    if archive is None:
        raise ValueError("--record requires --archive")
    members = archive_members(archive.read_bytes())
    objects = []
    for source, output in expected_objects(args.repo_root, archive.parent):
        expected = object_record(args, settings, source, output)
        actual = read_record(receipt_path(output))
        if actual != expected:
            raise ValueError(f"stale object receipt: {output}")
        if members.get(output.name) != output.read_bytes():
            raise ValueError(f"archive does not contain current object: {output.name}")
        objects.append(actual)
    if set(members) != {item["name"] for item in objects}:
        raise ValueError("archive contains unexpected/missing object members")
    archiver = settings["archiver"]
    assert isinstance(archiver, dict)
    record = {"format_version": 1, "settings": settings,
              "archive_sha256": sha256(archive.read_bytes()), "objects": objects,
              "generated_sha256": sha256((archive.parent / "generated_config.c").read_bytes()),
              "archive_command": [str(archiver["path"]), "rcsD", str(archive) + ".tmp",
                                  *[str(path) for _source, path in
                                    expected_objects(args.repo_root, archive.parent)]]}
    atomic_write(receipt_path(archive), canonical(record))


def verify_archive(args: argparse.Namespace) -> None:
    archive = args.archive
    if archive is None or not archive.is_absolute():
        raise ValueError("verification requires an absolute --archive path")
    raw = archive.read_bytes()
    record = read_record(receipt_path(archive))
    if record.get("format_version") != 1:
        raise ValueError("unsupported archive receipt format")
    if record.get("archive_sha256") != sha256(raw):
        raise ValueError("archive SHA-256 mismatch")
    settings = record.get("settings")
    if not isinstance(settings, dict):
        raise ValueError("missing archive build settings")
    expected = build_settings(args.repo_root, args.compiler, args.archiver,
                              args.target, args.profile)
    compare_settings(settings, expected)
    generated = archive.parent / "generated_config.c"
    if record.get("generated_sha256") != sha256(generated.read_bytes()):
        raise ValueError("generated C SHA-256 mismatch")
    members = archive_members(raw)
    objects = record.get("objects")
    if not isinstance(objects, list) or len(objects) != 2:
        raise ValueError("missing compilation records")
    wanted = expected_objects(args.repo_root, archive.parent)
    if set(members) != {output.name for _source, output in wanted}:
        raise ValueError("unexpected/missing archive members")
    for entry, (source, output) in zip(objects, wanted, strict=True):
        if not isinstance(entry, dict):
            raise ValueError("invalid compilation record")
        if entry.get("sha256") != sha256(members[output.name]):
            raise ValueError(f"archive member SHA-256 mismatch: {output.name}")
        if entry.get("settings_sha256") != sha256(canonical(settings)):
            raise ValueError(f"stale compiled settings: {output.name}")
        if entry.get("source_sha256") != sha256(source.read_bytes()):
            raise ValueError(f"stale compiled source: {source}")
        if entry.get("compile_command") != compile_command(args, settings, source, output):
            raise ValueError(f"mismatched compiler command/flags: {output.name}")
        if entry.get("name") != output.name or entry.get("source") != str(source):
            raise ValueError(f"mismatched object identity: {output.name}")
    archiver = settings["archiver"]
    assert isinstance(archiver, dict)
    command = [str(archiver["path"]), "rcsD", str(archive) + ".tmp",
               *[str(output) for _source, output in wanted]]
    if record.get("archive_command") != command:
        raise ValueError("mismatched archive command")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, help="Absolute policy archive path")
    parser.add_argument("--repo-root", required=True, type=Path,
                        help="Current source checkout root")
    parser.add_argument("--target", default=TARGET, help="Selected native target")
    parser.add_argument("--profile", default=PROFILE, type=int,
                        help="Selected admission profile")
    parser.add_argument("--compiler", default="gcc", help="Existing GCC executable")
    parser.add_argument("--archiver", default="ar", help="Existing GNU ar executable")
    parser.add_argument("--settings", type=Path, help="Captured build settings path")
    parser.add_argument("--source", type=Path, help="Compiled source for object record")
    parser.add_argument("--object", type=Path, help="Compiled object for object record")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--write-build-settings", type=Path,
                       help="Private Make mode: refresh content-stable settings")
    group.add_argument("--record-object", action="store_true",
                       help="Private Make mode: record successful compilation")
    group.add_argument("--record", action="store_true",
                       help="Private Make mode: record a fully built archive")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.repo_root = args.repo_root.resolve()
    try:
        if args.write_build_settings is not None:
            settings = build_settings(args.repo_root, args.compiler, args.archiver,
                                      args.target, args.profile)
            atomic_write(args.write_build_settings, canonical(settings))
        elif args.record_object:
            settings = checked_settings(args)
            if args.source is None or args.object is None:
                raise ValueError("--record-object requires --source and --object")
            expected = dict(expected_objects(args.repo_root, args.object.parent))
            if expected.get(args.source) != args.object:
                raise ValueError("unexpected source/object identity")
            record = object_record(args, settings, args.source, args.object)
            atomic_write(receipt_path(args.object), canonical(record))
        elif args.record:
            record_archive(args, checked_settings(args))
        else:
            verify_archive(args)
    except (OSError, ValueError, KeyError, TypeError, AssertionError,
            subprocess.SubprocessError) as exc:
        print(f"drop-in archive verification failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
