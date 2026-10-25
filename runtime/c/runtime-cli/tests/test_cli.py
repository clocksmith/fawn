#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""doe-c-runtime on the selected Vulkan adapter: example streams execute, their
trace artifacts pass pipeline/trace/replay.py, the row hash chain equals the
doe-zig-runtime rows for the same streams, an output oracle matches its
independent reference digest, and unsupported arguments, commands and oracle
mismatches fail with a nonzero exit. The adapter that ran is printed from the
trace meta; a CPU (fallback) adapter is not GPU evidence."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
EXAMPLES = REPO / "examples"
KERNELS = REPO / "bench" / "kernels"
REPLAY = REPO / "pipeline" / "trace" / "replay.py"

# Final row hashes doe-zig-runtime wrote for these streams (db0979828, lavapipe).
ZIG_FINAL_HASH = {
    "p0_resource_lifecycle": "0xb51cc5ee8f6a8133",
    "rmsnorm_tree": "0x3ab94f41b34d8349",
    "workgroup_atomic": "0xd00d567969d44b72",
}


def run(binary: Path, work: Path, name: str, *extra: str, commands: Path | None = None) -> tuple[int, str, dict, list]:
    jsonl = work / f"{name}.jsonl"
    meta = work / f"{name}.meta.json"
    for path in (jsonl, meta):
        if path.exists():
            path.unlink()
    stream = commands or EXAMPLES / f"{name}_commands.json"
    argv = [str(binary), "--commands", str(stream), "--execute", "--backend", "native",
            "--kernel-root", str(KERNELS), "--trace-jsonl", str(jsonl), "--trace-meta", str(meta), *extra]
    result = subprocess.run(argv, capture_output=True, text=True, timeout=900, cwd=REPO)
    meta_doc = json.loads(meta.read_text()) if meta.exists() else {}
    rows = [json.loads(line) for line in jsonl.read_text().splitlines() if line.strip()] if jsonl.exists() else []
    return result.returncode, result.stderr, meta_doc, rows


def replay_passes(work: Path, name: str) -> bool:
    result = subprocess.run([sys.executable, str(REPLAY), "--trace-jsonl", str(work / f"{name}.jsonl"),
                             "--trace-meta", str(work / f"{name}.meta.json")], capture_output=True, text=True)
    return result.returncode == 0 and result.stdout.strip() == "PASS"


def check(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"FAIL: {message}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)

    adapter = None
    for name, final_hash in ZIG_FINAL_HASH.items():
        code, stderr, meta, rows = run(args.binary, args.work, name, "--validate-output-oracles")
        check(code == 0, f"{name} exited {code}: {stderr}")
        check(replay_passes(args.work, name), f"{name} artifacts fail pipeline/trace/replay.py")
        check(meta["hash"] == final_hash, f"{name} final hash {meta['hash']} != doe-zig-runtime {final_hash}")
        check(all(row["executionStatus"] == "ok" for row in rows), f"{name} has a failed row")
        check(meta["executionSuccessCount"] == len(rows) == meta["commandCount"], f"{name} counts")
        adapter = meta["adapter"]
    rmsnorm = json.loads((args.work / "rmsnorm_tree.meta.json").read_text())
    check(rmsnorm["executionDispatchCount"] == 100 and rmsnorm["executionSubmitCount"] == 2, "rmsnorm dispatch/submit counts")
    atomic = json.loads((args.work / "workgroup_atomic.meta.json").read_text())
    check(atomic["outputOracleCount"] == 1 and atomic["outputOracleMatchedCount"] == 1, "workgroup_atomic oracle did not match")
    check(atomic["outputOracleActualSha256"] == atomic["outputOracleExpectedSha256"], "oracle digests differ")

    # --command-repeat continues one hash chain over the replayed stream.
    code, stderr, meta, rows = run(args.binary, args.work, "p0_resource_lifecycle", "--command-repeat", "2")
    check(code == 0 and len(rows) == 10 and meta["commandCount"] == 10, f"command repeat: {code} {stderr}")
    check(replay_passes(args.work, "p0_resource_lifecycle"), "command repeat artifacts fail replay")

    # A wrong expected digest is a failure, not a pass.
    stream = json.loads((EXAMPLES / "workgroup_atomic_commands.json").read_text())
    stream[0]["output_oracle"]["expected_sha256"] = "0" * 64
    mismatch = args.work / "oracle_mismatch_commands.json"
    mismatch.write_text(json.dumps(stream))
    code, stderr, meta, _ = run(args.binary, args.work, "oracle_mismatch", "--validate-output-oracles", commands=mismatch)
    check(code == 1 and meta["outputOracleFailedCount"] == 1 and "mismatch" in stderr, f"oracle mismatch: {code} {stderr}")

    # Unsupported input fails before device work with exit code 2.
    rejected = [
        (["--quirks", str(EXAMPLES / "quirks" / "amd_radv_noop_list.json")], "quirk matching"),
        (["--queue-sync-mode", "deferred"], "per-command"),
        (["--replay", "x"], "not implemented"),
        (["--gpu-timestamp-mode", "require"], "GPU timestamps"),
    ]
    for extra, needle in rejected:
        code, stderr, _, _ = run(args.binary, args.work, "rmsnorm_tree", *extra)
        check(code == 2 and needle in stderr, f"{extra}: exit {code}: {stderr}")
    code, stderr, _, _ = run(args.binary, args.work, "copy", commands=EXAMPLES / "copy_buffer_to_buffer_4kb_commands.json")
    check(code == 2 and "not implemented" in stderr, f"copy stream: exit {code}: {stderr}")

    print(f"PASS doe-c-runtime: 3 example streams with doe-zig-runtime hash chains, replay-valid artifacts, "
          f"an independent oracle match, command repeat, an oracle mismatch and 5 rejections on "
          f"{adapter.get('device')} ({adapter.get('adapterType')}, {adapter.get('backend')})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
