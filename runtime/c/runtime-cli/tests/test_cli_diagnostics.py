#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise retained public WebGPU diagnostic streams and honest strict failures."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from test_cli import EXAMPLES, check, replay_passes, run

FIXTURES = (
    "async_pipeline_diagnostics", "p0_render_pixel_local_storage_barrier",
    "p0_render_pixel_local_storage_barrier_macro", "p1_capability_introspection",
    "p1_capability_introspection_macro", "p1_resource_table_immediates",
    "p1_resource_table_immediates_macro", "p2_lifecycle_refcount",
    "p2_lifecycle_refcount_macro",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Runtime executable")
    parser.add_argument("--work", type=Path, required=True, help="Artifact directory")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    for name in FIXTURES:
        code, error, meta, rows = run(args.binary, args.work, name, "--quirk-mode", "off")
        check(code == 0, f"{name}: {error}")
        check(len(rows) == 1 and rows[0]["executionStatus"] == "ok", f"{name}: completion")
        check(meta["executionSubmitCount"] == meta["executionDispatchCount"] == 0,
              f"{name}: recorded diagnostics must not claim submissions")
        check("public_webgpu" in rows[0]["executionStatusMessage"], f"{name}: path identity")
        check(replay_passes(args.work, name), f"{name}: replay")
        mode = json.loads((EXAMPLES / f"{name}_commands.json").read_text(encoding="utf-8"))[0].get("mode")
        receipts = meta.get("shaderSourceReceipts", [])
        expected = 2 if mode == "lifecycle_refcount" else int(mode not in (
            "capability_introspection", "resource_table_immediates"))
        check(len(receipts) == expected, f"{name}: compiled shader receipt count")
    cases = [
        ("full", {"mode": "full", "featurePolicy": "emulate", "repeat": 2}, 0, None),
        ("strict_table", {"mode": "resource_table_immediates"}, 1, "diagnostic_extension_unavailable"),
        ("strict_pixel", {"mode": "pixel_local_storage"}, 1, "diagnostic_extension_unavailable"),
        ("zero", {"iterations": 0}, 2, None),
        ("ambiguous", {"iterations": 1, "repeat": 1}, 2, None),
        ("mode", {"mode": "unsupported"}, 2, None),
        ("policy", {"featurePolicy": "automatic"}, 2, None),
    ]
    for name, fields, expected, status in cases:
        commands = args.work / f"{name}.commands.json"
        commands.write_text(json.dumps([{"kind": "pipeline_async_diagnostics", **fields}]), encoding="utf-8")
        code, error, meta, rows = run(args.binary, args.work, name, "--quirk-mode", "off", commands=commands)
        check(code == expected, f"{name}: expected exit {expected}, got {code}: {error}")
        if status:
            check(rows[-1]["executionStatusCode"] == status, f"{name}: honest extension failure")
            check(meta["executionSuccessCount"] == 0, f"{name}: no synthetic success")
        if expected == 2:
            check(not rows and not meta, f"{name}: rejected before device work")
    print(f"PASS public WebGPU diagnostics: {args.binary}; adapter={meta.get('adapterInfo', 'see fixture receipts')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
