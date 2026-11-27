"""Interleaved snapshot-branch timing: Dawn D3, Dawn D5 and Doe E.

Runs ``bench/executors/snapshot-branch-native.c`` once per process in the order
``D3 D5 E E D5 D3`` per repeat and workload, keeps every run (failed ones
included) in a JSONL receipt file, checks each run against the CPU oracle
(``bench/oracles/snapshot_branch_reference_v1.c``) and prints medians with
min-max spreads. The caller provides the Vulkan environment (ICD and loader
search paths); this runner lowers its own priority instead of using
``nice(1)``, which would drop the loader variables on Darwin.

Example::

    python3 -m bench.runners.snapshot_branch_timing --dawn-exe out/snapshot-branch-dawn \
        --doe-exe out/snapshot-branch-doe --reference out/reference.json \
        --branches 32 --size-divisor 8 --repeats 2 --out out/receipts.jsonl
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ORDER = ("D3", "D5", "E", "E", "D5", "D3")
WORKLOADS = ("w", "scratch")
MIB = 1024.0 * 1024.0


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--dawn-exe", type=Path, required=True, help="executor built against Dawn")
    parser.add_argument("--doe-exe", type=Path, required=True, help="executor built with -DSNAPSHOT_BRANCH_DOE")
    parser.add_argument("--reference", type=Path, required=True, help="oracle JSON for the same sizes")
    parser.add_argument("--branches", type=int, default=32)
    parser.add_argument("--size-divisor", type=int, default=1, help="1 = ledger sizes; 8 for software dry runs")
    parser.add_argument("--repeats", type=int, default=2, help="repeats of the D3 D5 E E D5 D3 order")
    parser.add_argument("--workloads", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    parser.add_argument("--timeout", type=float, default=120.0, help="seconds per run")
    parser.add_argument("--nice", type=int, default=10)
    parser.add_argument("--out", type=Path, required=True, help="JSONL receipts, one line per run")
    return parser.parse_args(argv)


def _run_one(args: argparse.Namespace, arm: str, workload: str, sequence: int) -> dict[str, Any]:
    exe = args.doe_exe if arm == "E" else args.dawn_exe
    command = [str(exe), arm, workload, str(args.branches), str(args.size_divisor)]
    started = time.time()
    row: dict[str, Any] = {"sequence": sequence, "arm": arm, "workload": workload, "command": command,
                           "started_unix": started}
    try:
        done = subprocess.run(command, capture_output=True, text=True, timeout=args.timeout, check=False)
        row.update(exit_code=done.returncode, stderr=done.stderr[-2000:])
        lines = [line for line in done.stdout.splitlines() if line.strip()]
        row["receipt"] = json.loads(lines[-1]) if done.returncode == 0 and lines else None
    except subprocess.TimeoutExpired:
        row.update(exit_code=None, stderr="timeout", receipt=None)
    except json.JSONDecodeError as error:
        row.update(receipt=None, parse_error=str(error))
    row["process_seconds"] = time.time() - started
    row["checks"] = _check(row.get("receipt"), args, workload)
    return row


def _check(receipt: dict[str, Any] | None, args: argparse.Namespace, workload: str) -> dict[str, Any]:
    if receipt is None:
        return {"ok": False, "reason": "no receipt"}
    expected = args.reference_json[workload]
    outputs = receipt["outputs"]
    mismatched = [b for b, digest in enumerate(outputs) if digest != expected["branches"][b]]
    checks = {
        "sizes_match": receipt["branches"] == args.branches and receipt["size_divisor"] == args.size_divisor,
        "outputs_match_reference": not mismatched and len(outputs) == args.branches,
        "mismatched_branches": mismatched,
        "base_before_matches_reference": receipt["base_before"] == expected["base"],
        "base_unchanged": receipt["base_after"] == receipt["base_before"],
        "no_device_errors": receipt["device_errors"] == 0,
        "cpu_adapter": receipt["adapter"]["type"] == 3,
    }
    checks["ok"] = all(value for key, value in checks.items() if key != "mismatched_branches")
    return checks


def _spread(values: list[float]) -> str:
    if not values:
        return "n/a"
    return f"{statistics.median(values):.1f} [{min(values):.1f}-{max(values):.1f}]"


def _summary(rows: list[dict[str, Any]], workload: str) -> list[str]:
    lines = [f"workload {workload}: median [min-max] over runs",
             "arm  runs ok  setup ms              per-branch ms         total ms              "
             "copy MiB  submits  waits  peak incr MiB"]
    for arm in ("D3", "D5", "E"):
        mine = [row for row in rows if row["workload"] == workload and row["arm"] == arm]
        good = [row["receipt"] for row in mine if row["checks"]["ok"]]
        timing = [r["timing_ns"] for r in good]
        setup = [t["setup"] / 1e6 for t in timing]
        per_branch = [statistics.mean(t["branch"][1:]) / 1e6 for t in timing if len(t["branch"]) > 1]
        total = [t["total"] / 1e6 for t in timing]
        copy = sorted({round(r["copy_bytes"] / MIB, 2) for r in good})
        submits = sorted({r["submits"] for r in good})
        waits = sorted({r["waits"] for r in good})
        peak = [r["memory"]["incremental"] / MIB for r in good]
        lines.append(f"{arm:<4} {len(mine):>4} {len(good):>2}  {_spread(setup):<21} {_spread(per_branch):<21} "
                     f"{_spread(total):<21} {','.join(map(str, copy)):>8}  {','.join(map(str, submits)):>7}  "
                     f"{','.join(map(str, waits)):>5}  {_spread(peak)}")
    return lines


def main(argv: list[str]) -> int:
    args = _parse_args(argv)
    args.reference_json = json.loads(args.reference.read_text())
    if (args.reference_json["branches"] < args.branches
            or args.reference_json["size_divisor"] != args.size_divisor):
        raise SystemExit("reference was computed for other sizes")
    os.nice(args.nice)
    rows: list[dict[str, Any]] = []
    sequence = 0
    with args.out.open("w") as out:
        for workload in args.workloads:
            for _ in range(args.repeats):
                for arm in ORDER:
                    row = _run_one(args, arm, workload, sequence)
                    sequence += 1
                    rows.append(row)
                    out.write(json.dumps(row) + "\n")
                    out.flush()
                    status = "ok" if row["checks"]["ok"] else f"FAILED {row['checks']}"
                    print(f"{sequence:>3} {workload:<7} {arm:<2} {row['process_seconds']:6.2f} s {status}",
                          file=sys.stderr)
    digests: dict[str, set[str]] = {}
    for row in rows:
        if row["receipt"] is not None:
            digests.setdefault(row["workload"], set()).add(json.dumps(row["receipt"]["outputs"]))
    for workload in args.workloads:
        print("\n".join(_summary(rows, workload)))
        identical = len(digests.get(workload, set())) == 1
        print(f"outputs identical across all {workload} runs: {identical}\n")
    return 0 if all(row["checks"]["ok"] for row in rows) else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
