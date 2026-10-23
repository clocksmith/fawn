#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Run the unchanged six source tests separately from every C build path."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zig", type=Path, required=True,
                        help="Explicit already-approved Zig 0.15.2 executable")
    parser.add_argument("--evidence", type=Path, required=True,
                        help="New evidence directory for closure, cache and log")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[4]
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=False)
    source = root / "runtime/zig/src"
    closure = evidence / "source"
    start = source / "native/support/doe_future_ids.zig"
    expected = "be44acc738915f39412cfc4108f8204e31acbe59551fbc92c613d67d9ad59e92"
    if hashlib.sha256(start.read_bytes()).hexdigest() != expected:
        raise ValueError("Original future owner does not match frozen source hash")
    pending = [start]
    hashes: dict[str, str] = {}
    while pending:
        path = pending.pop().resolve()
        relative = path.relative_to(source)
        if str(relative) in hashes:
            continue
        data = path.read_bytes()
        hashes[str(relative)] = hashlib.sha256(data).hexdigest()
        target = closure / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        for name in re.findall(rb'@import\("([^"]+)"\)', data):
            if name.endswith(b".zig"):
                pending.append(path.parent / name.decode("utf-8"))
    header = root / "runtime/zig/vendor/webgpu-headers/webgpu.h"
    shutil.copyfile(header, closure / "webgpu.h")
    hashes["webgpu.h"] = hashlib.sha256(header.read_bytes()).hexdigest()
    (closure / "test_root.zig").write_text(
        'test { _ = @import("native/support/doe_future_ids.zig"); }\n',
        encoding="utf-8")
    (evidence / "source-hashes.json").write_text(
        json.dumps(hashes, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    command = [str(args.zig.resolve()), "test", str(closure / "test_root.zig"),
               "-I", str(closure), "-lc", "--test-filter", "",
               "--cache-dir", str(evidence / "local-cache"),
               "--global-cache-dir", str(evidence / "global-cache")]
    (evidence / "command.json").write_text(json.dumps(command, indent=2) + "\n",
                                           encoding="utf-8")
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    (evidence / "run.log").write_text(result.stdout + result.stderr,
                                      encoding="utf-8")
    print(result.stdout + result.stderr, end="")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
