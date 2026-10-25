#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real window-backed surface fixtures, state errors, resize and cleanup."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from test_cli import check, replay_passes, run


def operation(kind: str, **fields: object) -> dict:
    return {"kind": "surface_" + kind, "handle": 41001, **fields}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Runtime executable")
    parser.add_argument("--work", type=Path, required=True, help="Artifact directory")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    adapter = None
    for name in ("surface_presentation", "surface_full_presentation"):
        code, error, meta, rows = run(args.binary, args.work, name, "--quirk-mode", "off")
        check(code == 0, f"{name}: {error}")
        check(all(row["executionStatus"] == "ok" for row in rows), f"{name}: lifecycle completion")
        check(replay_passes(args.work, name), f"{name}: replay")
        check(meta["executionSubmitCount"] == 0, f"{name}: presentation is not queue submission")
        adapter = meta["adapterInfo"]
    create = operation("create")
    configure = operation("configure", width=64, height=64, format="bgra8unorm")
    acquire = operation("acquire")
    cases = [
        ("surface_lifetime_diagnostic", [create, {"kind": "async_diagnostics", "mode": "lifecycle_refcount"},
                                          operation("release")], 0, None),
        ("resize", [create, configure, acquire, operation("present"),
                    operation("configure", width=96, height=48, format="bgra8unorm"),
                    acquire, operation("present"), operation("release")], 0, None),
        ("reconfigure_acquired", [create, configure, acquire, configure, acquire, operation("present")], 0, None),
        ("release_acquired", [create, configure, acquire, operation("release"), create, operation("release")], 0, None),
        ("implicit_close_acquired", [create, configure, acquire], 0, None),
        ("missing", [operation("capabilities")], 1, "surface_missing"),
        ("duplicate", [create, create], 1, "surface_exists"),
        ("unconfigured", [create, acquire], 1, "surface_not_configured"),
        ("present_unacquired", [create, configure, operation("present")], 1, "surface_not_acquired"),
        ("double_acquire", [create, configure, acquire, acquire], 1, "surface_already_acquired"),
        ("acquire_after_unconfigure", [create, configure, operation("unconfigure"), acquire], 1, "surface_not_configured"),
        ("zero", [operation("configure", width=0, height=64)], 2, None),
        ("alias_collision", [{**create, "surfaceHandle": 41001}], 2, None),
        ("unsupported_hint", [{**configure, "toneMappingMode": "extended"}], 2, None),
    ]
    for name, stream, expected, status in cases:
        commands = args.work / f"{name}.commands.json"
        commands.write_text(json.dumps(stream), encoding="utf-8")
        code, error, meta, rows = run(args.binary, args.work, name, "--quirk-mode", "off", commands=commands)
        check(code == expected, f"{name}: expected {expected}, got {code}: {error}")
        if status:
            check(rows[-1]["executionStatusCode"] == status, f"{name}: precise lifecycle failure")
        if expected == 0:
            check(replay_passes(args.work, name), f"{name}: replay")
        if expected == 2:
            check(not rows and not meta, f"{name}: reject before device work")
    print(f"PASS surface fixtures, resize, acquired-frame cleanup and state failures: {adapter}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
