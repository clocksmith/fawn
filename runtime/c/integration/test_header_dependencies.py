#!/usr/bin/env python3
"""Exercise actual Make dependency edges in an isolated source copy.

The optional preimage demonstrates the old bug without editing the source tree.
No accepted semantic test is changed or used as a dependency oracle.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--makefile", type=Path, help="Optional historical Makefile preimage")
    parser.add_argument("--expect-stale", action="store_true", help="Require the historical stale result")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[3]
    with tempfile.TemporaryDirectory(prefix="doe-header-dependency-") as temporary:
        root = Path(temporary)
        owner = root / "runtime/c/error-scopes"
        owner.mkdir(parents=True)
        original = source / "runtime/c/error-scopes"
        for directory in ("src", "include", "integration"):
            shutil.copytree(original / directory, owner / directory)
        shutil.copyfile(args.makefile or original / "Makefile", owner / "Makefile")
        header = root / "runtime/zig/vendor/webgpu-headers/webgpu.h"
        header.parent.mkdir(parents=True)
        shutil.copyfile(source / header.relative_to(root), header)
        command = ["make", "--no-print-directory", "all"]
        subprocess.run(command, cwd=owner, check=True, capture_output=True)
        products = [owner / "build" / name for name in
                    ("error_scopes.o", "doe_error_scopes_zig_bridge.o", "libdoe_error_scopes.a")]
        before = {path.name: path.stat().st_mtime_ns for path in products}
        time.sleep(0.02)
        with header.open("a", encoding="utf-8") as stream:
            stream.write("\n/* isolated incremental dependency probe */\n")
        completed = subprocess.run(command, cwd=owner, check=True, capture_output=True, text=True)
        rebuilt = {path.name: path.stat().st_mtime_ns > before[path.name] for path in products}
        expected = not args.expect_stale
        passed = all(value == expected for value in rebuilt.values())
        print(json.dumps({"passed": passed, "expected_rebuilt": expected,
                          "rebuilt": rebuilt, "make_output": completed.stdout},
                         indent=2, sort_keys=True))
        return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
