#!/usr/bin/env python3
"""Enforce the mechanically checkable rules of runtime/c/STYLE.md.

Each failure names its rule:
  size     C, C++ and Objective-C sources/headers have at most 999 physical lines.
  license  The first three lines carry an SPDX-License-Identifier.
  guard    Headers have an #ifndef/#define include guard or #pragma once.
  pragma   No compiler diagnostic suppression pragmas.
  flags    Owner Makefiles that define CFLAGS compile strict C11 with -Werror.
  adapter  Native public consumers select adapters only through
           tests/adapter_class.h (no direct forceFallbackAdapter or CPU-type checks);
           tests/backend_selection.c tests fallback semantics themselves.

Frozen evidence is exempt from license and guard, never from size: files under
an independent_tests* directory, and files whose exact bytes still match a
digest in a committed runtime/c *.sha256 seal. A sealed file that has changed
is live source again and must comply. Judgment rules (ownership, failure
publication, lifetimes) stay in STYLE.md and review; this is not a quality score.
"""
from __future__ import annotations

import hashlib
from pathlib import Path
import re
import subprocess
import sys

from check_source_size import MAX_LINES, check as check_size

ROOT = Path(__file__).resolve().parents[3]
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".hpp", ".m", ".mm"}
HEADER_SUFFIXES = {".h", ".hpp"}
STRICT_FLAGS = ("-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror")
SEAL_LINE = re.compile(r"([0-9a-f]{64})\s+\*?(\S.*)")
GUARD = re.compile(rb"^\s*#\s*ifndef\s+(\w+)\s*\n\s*#\s*define\s+\1\b", re.MULTILINE)
PRAGMA_ONCE = re.compile(rb"^\s*#\s*pragma\s+once\b", re.MULTILINE)
SUPPRESSION = re.compile(rb"#\s*pragma\s+(GCC|clang)\s+diagnostic\s+(ignored|warning)")
CFLAGS = re.compile(r"^\s*CFLAGS\s*[:+?]?=", re.MULTILINE)
# Owner-scoped source patterns: (path prefix, pattern, exempt paths, rule, message).
SCOPED = (
    ("runtime/c/native/tests/", re.compile(rb"forceFallbackAdapter\s*=|WGPUAdapterType_CPU"),
     {"runtime/c/native/tests/adapter_class.h", "runtime/c/native/tests/backend_selection.c"},
     "adapter", "select adapters through tests/adapter_class.h"),
)


def tracked(root: Path) -> list[str]:
    output = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z",
         "--", "runtime/c"], cwd=root)
    return sorted({p.decode() for p in output.split(b"\0") if p})


def sealed_files(root: Path, paths: list[str]) -> set[str]:
    """Paths whose current bytes equal a digest recorded by a committed seal."""
    sealed: set[str] = set()
    for seal in (p for p in paths if p.endswith(".sha256")):
        seal_path = root / seal
        if not seal_path.is_file():
            continue
        for line in seal_path.read_text(errors="replace").splitlines():
            match = SEAL_LINE.match(line.strip())
            if not match:
                continue
            digest, name = match.groups()
            for candidate in (Path(name), Path(seal).parent / name):
                full = root / candidate
                if full.is_file() and not full.is_symlink():
                    if hashlib.sha256(full.read_bytes()).hexdigest() == digest:
                        sealed.add(candidate.as_posix())
                    break
    return sealed


def frozen(path: str, sealed: set[str]) -> bool:
    return any(part.startswith("independent_tests") for part in Path(path).parts) or path in sealed


def source_failures(root: Path, path: str, is_frozen: bool) -> list[str]:
    full = root / path
    if (error := check_size(full)) is not None:
        return [f"size: {error}"]
    data = full.read_bytes()
    failures = []
    if SUPPRESSION.search(data):
        failures.append(f"pragma: {path}: diagnostic suppression; fix the warning instead")
    for prefix, pattern, exempt, rule, message in SCOPED:
        if path.startswith(prefix) and path not in exempt and pattern.search(data):
            failures.append(f"{rule}: {path}: {message}")
    if is_frozen:
        return failures
    if b"SPDX-License-Identifier:" not in b"\n".join(data.splitlines()[:3]):
        failures.append(f"license: {path}: no SPDX-License-Identifier in the first three lines")
    if Path(path).suffix in HEADER_SUFFIXES and not (GUARD.search(data) or PRAGMA_ONCE.search(data)):
        failures.append(f"guard: {path}: header has no include guard")
    return failures


def makefile_failures(root: Path, path: str) -> list[str]:
    text = (root / path).read_text()
    if not CFLAGS.search(text):
        return []
    missing = [flag for flag in STRICT_FLAGS if flag not in text]
    return [f"flags: {path}: CFLAGS owner lacks {' '.join(missing)}"] if missing else []


def run(root: Path, paths: list[str]) -> tuple[int, list[str]]:
    sealed = sealed_files(root, paths)
    sources = [p for p in paths if Path(p).suffix in SOURCE_SUFFIXES]
    failures = []
    for path in sources:
        failures.extend(source_failures(root, path, frozen(path, sealed)))
    for path in (p for p in paths if Path(p).name == "Makefile"):
        failures.extend(makefile_failures(root, path))
    return len(sources), failures


def main() -> int:
    count, failures = run(ROOT, tracked(ROOT))
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"C style check: {count} sources, {len(failures)} failures; line limit {MAX_LINES}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
