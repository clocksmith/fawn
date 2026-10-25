"""Fail when a source file declares Zig tests that no test-suite binary runs.

Zig compiles a file's `test` declarations only when a test root reaches that
file, so a test added to an unreached file passes silently. This check reads
the test symbols (`<module path>.test.<name>`) of every built suite binary and
compares them with the source files under src/ that declare tests. A file that
is deliberately not reached is listed with a reason under
`unreachedSourceTests` in config/zig-test-inventory.json; a listed file that a
suite does reach is reported as stale.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
INVENTORY_PATH = ROOT.parents[1] / "config" / "zig-test-inventory.json"
TEST_DECLARATION = re.compile(r"^test\s+\"", re.MULTILINE)
TEST_SYMBOL = re.compile(r"(src(?:\.[A-Za-z0-9_]+)+)\.test\.")


def declared_test_files() -> set[str]:
    return {
        path.relative_to(ROOT).as_posix()
        for path in (ROOT / "src").rglob("*.zig")
        if TEST_DECLARATION.search(path.read_text(encoding="utf-8"))
    }


def quarantined_csl_source(path: str) -> bool:
    # The versioned quarantine manifest owns the optional source boundary.
    manifest = json.loads((ROOT.parents[1] / "config/csl-quarantine.json").read_text())
    repository_path = "runtime/zig/" + path
    return repository_path.startswith(tuple(manifest["sourcePrefixes"])) or repository_path in manifest["sourceFiles"]


def module_name(path: str) -> str:
    return path.removesuffix(".zig").replace("/", ".")


def reached_modules(binaries: list[Path]) -> set[str]:
    modules: set[str] = set()
    for binary in binaries:
        result = subprocess.run(["nm", str(binary)], capture_output=True, text=True, check=False)
        if result.returncode != 0:
            raise SystemExit(f"nm failed on {binary}: {result.stderr.strip()}")
        modules.update(match.group(1) for match in TEST_SYMBOL.finditer(result.stdout))
    return modules


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-csl", action="store_true", help="include the explicit optional CSL suite")
    parser.add_argument("binaries", nargs="+", type=Path, help="unfiltered test-suite executables")
    args = parser.parse_args()
    inventory = json.loads(INVENTORY_PATH.read_text(encoding="utf-8"))
    allowed = {entry["path"]: entry.get("reason", "") for entry in inventory.get("unreachedSourceTests", [])}
    declared = declared_test_files()
    quarantined = set() if args.include_csl else {path for path in declared if quarantined_csl_source(path)}
    declared -= quarantined
    allowed = {path: reason for path, reason in allowed.items() if path not in quarantined}
    reached = reached_modules(args.binaries)
    errors: list[str] = []
    for path in sorted(declared):
        hit = module_name(path) in reached
        if not hit and path not in allowed:
            errors.append(f"tests in {path} are compiled by no test suite; add the file to a suite in "
                          "config/zig-test-inventory.json or list it under unreachedSourceTests with a reason")
        elif hit and path in allowed:
            errors.append(f"{path} is listed under unreachedSourceTests but a suite runs its tests")
    for path, reason in sorted(allowed.items()):
        if path not in declared:
            errors.append(f"unreachedSourceTests lists {path}, which declares no tests")
        elif not reason.strip():
            errors.append(f"unreachedSourceTests entry {path} needs a reason")
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"test reachability: {len(declared) - len(allowed)} source files with tests reached; "
          f"{len(allowed)} listed as deliberately unreached; {len(quarantined)} optional CSL files excluded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
