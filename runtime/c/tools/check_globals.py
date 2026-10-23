#!/usr/bin/env python3
"""Fail on writable process-global objects that are not explicitly owned.

STYLE.md forbids hidden mutable globals. This reads the symbol tables of built
artifacts (objdump -t) and collects data objects placed in writable .data/.bss.
Relocated constant tables (.data.rel.ro) and read-only data are not mutable state
and are ignored. Every remaining symbol must appear in mutable_globals.txt with its
owner source and the reason it is process-lifetime state; an entry no artifact
defines is stale and also fails, so the list cannot outlive the code.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

ALLOW = Path(__file__).with_name("mutable_globals.txt")
ARTIFACT_SUFFIXES = {".a", ".so", ".o", ".dylib"}
# objdump -t: "<addr> <flags> <section>\t<size> <name>"; O marks a data object.
SYMBOL = re.compile(r"^\S+\s+.{7}\s+(\.data|\.bss)\s+\S+\s+(\S+)$")
TOOLCHAIN = {"completed.0", "__dso_handle", "__TMC_END__"}


def allowed(path: Path) -> dict[str, str]:
    entries = {}
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split(None, 2)
        if len(fields) != 3:
            raise SystemExit(f"{path}:{number}: expected '<symbol> <owner source> <reason>'")
        entries[fields[0]] = fields[1]
    return entries


def artifacts(roots: list[Path]) -> list[Path]:
    found = []
    for root in roots:
        if root.is_file():
            found.append(root)
        elif root.is_dir():
            found.extend(p for p in root.rglob("*") if p.suffix in ARTIFACT_SUFFIXES and p.is_file())
        else:
            raise SystemExit(f"{root}: no such build artifact or directory")
    return sorted(found)


def writable_objects(path: Path) -> set[str]:
    output = subprocess.run(["objdump", "-t", str(path)], capture_output=True, text=True, check=True).stdout
    names = set()
    for line in output.splitlines():
        match = SYMBOL.match(line.strip())
        if match and " O " in line and match.group(2) not in TOOLCHAIN and not match.group(2).startswith("DW.ref."):
            names.add(match.group(2))
    return names


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("roots", nargs="+", type=Path, help="build directories or artifacts to scan")
    parser.add_argument("--allow", type=Path, default=ALLOW, help="owned mutable-global list")
    args = parser.parse_args(argv)
    permitted = allowed(args.allow)
    found: dict[str, set[str]] = {}
    paths = artifacts(args.roots)
    for path in paths:
        for name in writable_objects(path):
            found.setdefault(name, set()).add(path.name)
    failures = [f"unowned mutable global {name} in {', '.join(sorted(where))}"
                for name, where in sorted(found.items()) if name not in permitted]
    failures += [f"stale mutable_globals.txt entry {name} ({source}): no scanned artifact defines it"
                 for name, source in sorted(permitted.items()) if name not in found]
    for failure in failures:
        print(failure, file=sys.stderr)
    print(f"C mutable globals: {len(paths)} artifacts, {len(found)} writable objects, {len(failures)} failures")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
