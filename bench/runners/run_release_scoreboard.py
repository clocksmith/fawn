"""Run the required Doe-versus-Dawn release scoreboard on one backend.

For every row in ``config/release-scoreboard.json`` this runs each workload
once per provider through the existing ``bench/cli.py run`` front door (fresh
processes, provider order alternating by workload), compares the two receipts
with ``bench/cli.py compare`` and writes ``scoreboard-summary.json``. A failed
run, rejected comparison or skipped row is recorded with its status; nothing
is retried or dropped. ``bench/gates/release_scoreboard_gate.py`` decides
release-claim admission from the summary and the compare reports it names.

Overriding ``--iterations``/``--warmup`` or selecting ``--rows`` marks the
summary functional-only; such a summary can never admit a release claim.

Each backend entry declares its capability mode. ``full`` gives each provider
every capability its adapter reports. ``intersection`` first probes both
providers' adapters in fresh processes and exposes only the features and
limits both support, so the application selects the same shaders on both; the
set is recorded in the summary and in every receipt. In intersection mode a
workload neither provider can execute, with the same error, is recorded as not
comparable on this host, and one Dawn cannot execute while Doe executes is
recorded as a Dawn limitation, each with the exact error. Neither is a pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCOREBOARD = REPO_ROOT / "config" / "release-scoreboard.json"
CLI = ["bench/cli.py"]
PROBE = "bench/executors/release-scoreboard/probe-capabilities.js"
CAPABILITY_MODE_ENV = "FAWN_SCOREBOARD_CAPABILITY_MODE"
CAPABILITY_SET_ENV = "FAWN_SCOREBOARD_CAPABILITY_SET"
CAPABILITY_MODES = ("full", "intersection")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--backend", required=True, choices=["metal", "vulkan", "d3d12"],
                        help="Backend whose compare configs are run on this host.")
    parser.add_argument("--out", required=True, type=Path, help="New output directory for receipts and the summary.")
    parser.add_argument("--scoreboard", type=Path, default=DEFAULT_SCOREBOARD, help="Scoreboard definition.")
    parser.add_argument("--rows", default="", help="Comma-separated row ids (functional runs only).")
    parser.add_argument("--iterations", type=int, default=0, help="Override timed samples (functional runs only).")
    parser.add_argument("--warmup", type=int, default=-1, help="Override warmup samples (functional runs only).")
    parser.add_argument("--dry-run", action="store_true", help="Print the commands without running them.")
    return parser.parse_args(argv)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def run_command(command: list[str], dry_run: bool, env: dict[str, str] | None = None) -> int:
    print("+ " + " ".join(command), flush=True)
    if dry_run:
        return 0
    return subprocess.run(command, cwd=REPO_ROOT, check=False, env=env).returncode


def capability_environment(mode: str, capability_set: dict[str, Any] | None) -> dict[str, str]:
    """Child environment naming the capability mode; full mode carries no set."""
    env = dict(os.environ)
    env[CAPABILITY_MODE_ENV] = mode
    env.pop(CAPABILITY_SET_ENV, None)
    if mode == "intersection":
        assert capability_set is not None
        env[CAPABILITY_SET_ENV] = json.dumps(
            {"features": capability_set["features"], "limits": capability_set["limits"]}, sort_keys=True)
    return env


def probe_provider(provider: str, backend: str) -> dict[str, Any]:
    """One provider's adapter features and limits, from a fresh process."""
    env = capability_environment("full", None)
    result = subprocess.run(["node", PROBE, "--provider", provider, "--backend", backend], cwd=REPO_ROOT,
                            env=env, check=False, capture_output=True, text=True, timeout=300)
    if result.returncode != 0:
        raise RuntimeError(f"{provider} capability probe failed ({result.returncode}): {result.stderr.strip()}")
    return json.loads(result.stdout.strip().splitlines()[-1])


def intersect_capabilities(probes: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Features every provider reports; for limits both report, the less capable value."""
    values = list(probes.values())
    features = sorted(set.intersection(*(set(entry["features"]) for entry in values)))
    shared = set.intersection(*(set(entry["limits"]) for entry in values))
    limits = {}
    for name in sorted(shared):
        candidates = [entry["limits"][name] for entry in values]
        limits[name] = max(candidates) if name.startswith("min") else min(candidates)
    return {"features": features, "limits": limits}


def side_failure(path: Path | None) -> dict[str, Any] | None:
    """First failure a receipt's samples carry, and whether it was only the
    output check (the workload executed; its output was not admitted)."""
    if path is None:
        return None
    for sample in load_json(path).get("samples", []):
        meta = sample.get("traceMeta") or {}
        if meta.get("failureMessage"):
            return {"message": str(meta["failureMessage"]), "outputCheck": meta.get("outputCheckFailed") is True}
    return None


def side_capability(path: Path | None) -> Any:
    """The capability record of every sample, which must agree within a side."""
    if path is None:
        return None
    records = {json.dumps({key: (sample.get("traceMeta") or {}).get("capability", {}).get(key)
                           for key in ("mode", "exposedFeatures", "exposedLimits")}, sort_keys=True)
               for sample in load_json(path).get("samples", [])}
    return records.pop() if len(records) == 1 else "inconsistent"


def single_receipt(directory: Path) -> Path | None:
    receipts = sorted(directory.glob("run-artifacts/*/*.run.json"))
    if len(receipts) > 1:
        raise RuntimeError(f"{directory} holds {len(receipts)} run receipts; expected one fresh receipt")
    return receipts[0] if receipts else None


def run_side(config: dict[str, Any], side: str, product: str, workload_id: str, out: Path,
             iterations: int, warmup: int, dry_run: bool, env: dict[str, str]) -> tuple[Path | None, int]:
    comparability = config["comparability"]
    command = [
        sys.executable, *CLI, "run",
        "--product", product,
        "--executor-id", config[side]["executorId"],
        "--workloads", config["workloads"],
        "--workload-id", workload_id,
        "--iterations", str(iterations),
        "--warmup", str(warmup),
        "--comparability-mode", comparability["mode"],
        "--require-timing-class", comparability["requireTimingClass"],
        "--out", str(out),
    ]
    code = run_command(command, dry_run, env)
    return (None if dry_run else single_receipt(out)), code


def receipt_succeeded(path: Path | None) -> bool:
    if path is None:
        return False
    execution = load_json(path).get("execution", {})
    return isinstance(execution, dict) and execution.get("success") is True


def workload_verdict(report_path: Path, workload_id: str) -> dict[str, Any]:
    report = load_json(report_path)
    row = next((entry for entry in report.get("workloads", []) if entry.get("id") == workload_id), None)
    if row is None:
        return {"comparable": False, "reasons": ["compare report has no row for this workload"]}
    comparability = row.get("comparability", {})
    return {
        "comparable": comparability.get("comparable") is True,
        "reasons": comparability.get("reasons", []),
        "baselineP50Ms": (row.get("baselineStatsMs") or {}).get("p50Ms"),
        "comparisonP50Ms": (row.get("comparisonStatsMs") or {}).get("p50Ms"),
    }


def classify_failure(mode: str, products: dict[str, str],
                     failures: dict[str, dict[str, Any] | None]) -> tuple[str, list[str]] | None:
    """Intersection-mode labels for runs that could not execute; None keeps
    the ordinary status. An output-check failure means the workload executed,
    so it never makes a row not comparable by itself."""
    if mode != "intersection":
        return None
    executed = {side: failures[side] is None or failures[side]["outputCheck"] for side in failures}
    if not any(executed.values()) and failures["baseline"]["message"] == failures["comparison"]["message"]:
        return "not-comparable-on-host", [f"not comparable on this host: {failures['baseline']['message']}"]
    dawn = next((side for side, product in products.items() if product == "dawn"), None)
    other = "baseline" if dawn == "comparison" else "comparison"
    if dawn and not executed[dawn] and executed[other]:
        reasons = [f"Dawn limitation on this host: {failures[dawn]['message']}"]
        if failures[other]:
            reasons.append(f"{products[other]} executed; its output check: {failures[other]['message']}")
        return "dawn-limitation", reasons
    return None


def run_workload(config: dict[str, Any], scoreboard: dict[str, Any], workload_id: str, index: int,
                 row_out: Path, iterations: int, warmup: int, dry_run: bool, mode: str,
                 env: dict[str, str]) -> dict[str, Any]:
    out = row_out / workload_id
    sides = [("baseline", scoreboard["baselineProduct"]), ("comparison", scoreboard["comparisonProduct"])]
    if index % 2:
        sides.reverse()
    receipts: dict[str, Path | None] = {}
    exit_codes: dict[str, int] = {}
    for side, product in sides:
        receipts[side], exit_codes[side] = run_side(
            config, side, product, workload_id, out / side, iterations, warmup, dry_run, env)
    result: dict[str, Any] = {
        "workloadId": workload_id,
        "providerOrder": [product for _, product in sides],
        "runExitCodes": exit_codes,
        "runReceipts": {side: (path.relative_to(REPO_ROOT).as_posix() if path and path.is_relative_to(REPO_ROOT)
                               else str(path) if path else None) for side, path in receipts.items()},
    }
    if dry_run:
        result["status"] = "dry-run"
        return result
    failed = [side for side in ("baseline", "comparison") if not receipt_succeeded(receipts[side])]
    if receipts["baseline"] is None or receipts["comparison"] is None:
        result.update(status="unmeasured", failedSides=failed)
        return result
    report = out / "compare.json"
    run_command([sys.executable, *CLI, "compare", str(receipts["baseline"]), str(receipts["comparison"]),
                 "--out", str(report)], dry_run=False)
    if not report.exists():
        result.update(status="unmeasured", failedSides=failed, reasons=["compare produced no report"])
        return result
    verdict = workload_verdict(report, workload_id)
    status = "comparable" if verdict["comparable"] and not failed else ("run-failed" if failed else "rejected")
    capabilities = {side: side_capability(receipts[side]) for side in ("baseline", "comparison")}
    if capabilities["baseline"] != capabilities["comparison"] or "inconsistent" in capabilities.values():
        status = "rejected"
        verdict["comparable"] = False
        verdict["reasons"] = [f"capability sets differ between sides: {capabilities}", *verdict["reasons"]]
    failures = {side: side_failure(receipts[side]) for side in ("baseline", "comparison")}
    products = {"baseline": scoreboard["baselineProduct"], "comparison": scoreboard["comparisonProduct"]}
    label = classify_failure(mode, products, failures) if failed else None
    if label:
        status, reasons = label
        verdict["reasons"] = [*reasons, *verdict["reasons"]]
    result.update(verdict, status=status, failedSides=failed, failures=failures,
                  compareReport=str(report), compareReportSha256=sha256_file(report))
    return result


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    scoreboard = load_json(args.scoreboard)
    host = next(entry for entry in scoreboard["backends"] if entry["backend"] == args.backend)
    selected = {value.strip() for value in args.rows.split(",") if value.strip()}
    unknown = selected - {row["rowId"] for row in scoreboard["rows"]}
    if unknown:
        print(f"unknown rows: {sorted(unknown)}", file=sys.stderr)
        return 2
    if args.out.exists() and any(args.out.iterdir()) and not args.dry_run:
        print(f"{args.out} is not empty; use a new output directory", file=sys.stderr)
        return 2
    functional_only = bool(selected) or args.iterations > 0 or args.warmup >= 0
    mode = host.get("capabilityMode")
    if mode not in CAPABILITY_MODES:
        print(f"{args.backend}: scoreboard declares no valid capabilityMode ({mode!r})", file=sys.stderr)
        return 2
    capability: dict[str, Any] = {"mode": mode}
    if mode == "intersection" and not args.dry_run:
        probes = {product: probe_provider(product, args.backend)
                  for product in (scoreboard["baselineProduct"], scoreboard["comparisonProduct"])}
        capability.update(intersect_capabilities(probes), providers=probes)
        print(f"capability intersection: {', '.join(capability['features'])}", flush=True)
    # A dry run prints commands only, so it neither probes nor names a set.
    env = dict(os.environ) if args.dry_run else capability_environment(
        mode, capability if mode == "intersection" else None)
    rows = []
    for row in scoreboard["rows"]:
        if selected and row["rowId"] not in selected:
            rows.append({"rowId": row["rowId"], "status": "not-run", "workloads": []})
            continue
        config_path = REPO_ROOT / f"{host['configPrefix']}{row['configSuffix']}.json"
        config = load_json(config_path)
        iterations = args.iterations or int(config["run"]["iterations"])
        warmup = args.warmup if args.warmup >= 0 else int(config["run"]["warmup"])
        workloads = [
            run_workload(config, scoreboard, workload_id, index, args.out / row["rowId"],
                         iterations, warmup, args.dry_run, mode, env)
            for index, workload_id in enumerate(row["workloadIds"])
        ]
        statuses = {entry["status"] for entry in workloads}
        rows.append({
            "rowId": row["rowId"],
            "config": config_path.relative_to(REPO_ROOT).as_posix(),
            "configSha256": sha256_file(config_path),
            "iterations": iterations,
            "warmup": warmup,
            "status": "comparable" if statuses == {"comparable"} else "blocked",
            "workloads": workloads,
        })
    summary = {
        "schemaVersion": 1,
        "scoreboardVersion": scoreboard["scoreboardVersion"],
        "scoreboardSha256": sha256_file(args.scoreboard),
        "backend": args.backend,
        "functionalOnly": functional_only,
        "capabilityMode": mode,
        "capability": capability,
        "rows": rows,
    }
    if args.dry_run:
        return 0
    args.out.mkdir(parents=True, exist_ok=True)
    summary_path = args.out / "scoreboard-summary.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    for row in rows:
        print(f"{row['rowId']}: {row['status']}")
        for entry in row["workloads"]:
            print(f"  {entry['workloadId']}: {entry['status']}"
                  + (f" ({'; '.join(entry.get('reasons', [])[:2])})" if entry.get("reasons") else ""))
    print(f"summary: {summary_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
