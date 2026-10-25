#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Queue strategies retain copy output, row timing sums, and trace chains."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from test_cli import check, replay_passes, roundtrip_stream, run


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Runtime executable")
    parser.add_argument("--work", type=Path, required=True, help="Artifact directory")
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    stream, expected = roundtrip_stream()
    commands = args.work / "copy.commands.json"
    commands.write_text(json.dumps(stream), encoding="utf-8")
    baseline_hash = None
    for mode in ("process-events", "wait-any"):
        for sync in ("per-command", "deferred"):
            name = f"{mode}-{sync}"
            extra = ["--quirk-mode", "off", "--queue-wait-mode", mode, "--queue-sync-mode", sync,
                     "--webgpu-ffi-queue-wait-timeout-ns", "5000000000"]
            for handle in expected:
                extra += ["--readback-buffer", f"{handle}={args.work / f'{name}-{handle}.bin'}"]
            code, error, meta, rows = run(args.binary, args.work, name, *extra, commands=commands)
            check(code == 0, f"{name}: {error}")
            check(meta["queueSyncMode"] == sync and meta["queueWaitMode"] == mode, f"{name}: actual mode receipt")
            check(meta["executionTotalNs"] == sum(row["executionDurationNs"] for row in rows), f"{name}: total includes final drain")
            check(meta["executionSubmitWaitTotalNs"] == sum(row["executionSubmitWaitNs"] for row in rows),
                  f"{name}: wait total includes final drain")
            baseline_hash = baseline_hash or meta["hash"]
            check(meta["hash"] == baseline_hash and replay_passes(args.work, name), f"{name}: same replay chain")
            for handle, data in expected.items():
                actual = (args.work / f"{name}-{handle}.bin").read_bytes()
                check(actual == data, f"{name}: handle {handle} output bytes")
    # Upload batches smaller than their submission interval must still drain.
    commands.write_text(json.dumps([{"kind": "upload", "bytes": 1024}]), encoding="utf-8")
    code, error, meta, rows = run(args.binary, args.work, "partial-batch", "--upload-submit-every", "4", commands=commands)
    check(code == 0 and replay_passes(args.work, "partial-batch"), f"partial upload batch: {error}")
    check(meta["executionTotalNs"] == rows[0]["executionDurationNs"], "partial batch drain timing")
    print(f"PASS queue controls and drained copy bytes: {meta['adapterInfo']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
