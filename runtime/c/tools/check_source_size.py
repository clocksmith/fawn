#!/usr/bin/env python3
"""Enforce the C source/header physical-line ceiling, including new tracked work."""
from pathlib import Path
import subprocess
import sys

MAX_LINES = 999
ROOT = Path(__file__).resolve().parents[3]


def check(path: Path) -> str | None:
    if path.is_symlink():
        return f"{path}: source symlinks are not a size-accounting exemption"
    if not path.is_file():
        return f"{path}: source is missing or not a regular file"
    count = len(path.read_bytes().splitlines())
    if count > MAX_LINES:
        return f"{path}: {count} physical lines exceeds {MAX_LINES}"
    return None


def main() -> int:
    paths = sys.argv[1:]
    if not paths:
        output = subprocess.check_output(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard",
             "-z", "--", "runtime/c"], cwd=ROOT)
        paths = sorted(set(p.decode() for p in output.split(b"\0") if p))
    paths = [Path(p) for p in paths if Path(p).suffix in (".c", ".h")]
    failures = [error for path in paths if (error := check(ROOT / path))]
    for error in failures:
        print(error, file=sys.stderr)
    print(f"C size check: {len(paths)} files, {len(failures)} failures; limit {MAX_LINES}")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
