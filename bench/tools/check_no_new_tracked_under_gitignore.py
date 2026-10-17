#!/usr/bin/env python3
"""Reject ignored files newly staged or introduced since --base-ref.

Explicit paths may be checked instead. Git owns ignore/negation semantics;
retained historical debt is not an exemption for new additions. CI compares
its event base with HEAD, while ordinary local invocation checks the index.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


SCRIPT_REPO_ROOT = Path(__file__).resolve().parents[2]


def _detect_repo_root() -> Path:
    proc = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=Path.cwd(), capture_output=True, text=True, check=False,
    )
    if proc.returncode == 0 and proc.stdout.strip():
        return Path(proc.stdout.strip())
    return SCRIPT_REPO_ROOT


REPO_ROOT = _detect_repo_root()


def _added_paths(base_ref: str | None) -> list[str]:
    selection = [base_ref, "HEAD"] if base_ref is not None else ["--cached"]
    proc = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=A", "--no-renames",
         "-z", *selection, "--"],
        cwd=REPO_ROOT, capture_output=True, check=True,
    )
    return [os.fsdecode(path) for path in proc.stdout.split(b"\0") if path]


def _matches_gitignore(paths: list[str]) -> list[str]:
    if not paths:
        return []
    proc = subprocess.run(
        ["git", "check-ignore", "--no-index", "-z", "--stdin"],
        input=b"\0".join(os.fsencode(path) for path in paths) + b"\0",
        cwd=REPO_ROOT, capture_output=True, check=False,
    )
    if proc.returncode not in (0, 1):
        proc.check_returncode()
    return [os.fsdecode(path) for path in proc.stdout.split(b"\0") if path]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-ref", help="compare additions from this revision to HEAD",
    )
    parser.add_argument("paths", nargs="*", help="explicit paths instead of a Git diff")
    args = parser.parse_args(argv)
    if args.base_ref is not None and args.paths:
        parser.error("choose --base-ref or explicit paths, not both")
    try:
        paths = args.paths if args.paths else _added_paths(args.base_ref)
        matched = _matches_gitignore(paths)
    except subprocess.CalledProcessError as exc:
        print(
            f"ignore-policy check failed: {os.fsdecode(exc.stderr).strip()}",
            file=sys.stderr,
        )
        return 1
    if not matched:
        return 0
    print(
        "ERROR: refusing to add files that match a .gitignore pattern.",
        file=sys.stderr,
    )
    print(
        "Remove generated output from the index; legitimate source needs "
        "an explicit ignore exception.", file=sys.stderr,
    )
    for path in matched:
        print(f"  {path}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
