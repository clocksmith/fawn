"""Admit or block a release performance claim from the release scoreboard.

A claim for one backend is admissible only when a full (not functional-only)
scoreboard summary for that backend covers every row and workload declared in
``config/release-scoreboard.json`` and every workload's own compare report
still says comparable. Missing, failed, rejected, unmeasured or altered rows
block the claim and are listed. Comparability is re-read from each compare
report (bound by SHA-256), not taken from the summary.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SCOREBOARD = REPO_ROOT / "config" / "release-scoreboard.json"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--summary", required=True, type=Path, help="scoreboard-summary.json from the runner.")
    parser.add_argument("--backend", required=True, choices=["metal", "vulkan", "d3d12"],
                        help="Backend the release claim is for.")
    parser.add_argument("--scoreboard", type=Path, default=DEFAULT_SCOREBOARD, help="Scoreboard definition.")
    return parser.parse_args(argv)


def _report_comparable(entry: dict[str, Any]) -> str | None:
    """Return a blocking reason, or None when the bound report is comparable."""
    path_value = entry.get("compareReport")
    if not path_value:
        return "no compare report"
    path = Path(path_value)
    path = path if path.is_absolute() else REPO_ROOT / path
    if not path.is_file():
        return f"compare report missing: {path_value}"
    if hashlib.sha256(path.read_bytes()).hexdigest() != entry.get("compareReportSha256"):
        return "compare report changed after the run"
    report = json.loads(path.read_text(encoding="utf-8"))
    row = next((item for item in report.get("workloads", []) if item.get("id") == entry.get("workloadId")), None)
    if row is None:
        return "compare report has no row for this workload"
    if row.get("comparability", {}).get("comparable") is not True:
        reasons = row.get("comparability", {}).get("reasons", [])
        return "not comparable: " + "; ".join(str(reason) for reason in reasons[:3])
    return None


def blocking_reasons(scoreboard: dict[str, Any], summary: dict[str, Any], backend: str) -> list[str]:
    """List every reason the summary cannot admit a release claim."""
    reasons: list[str] = []
    if summary.get("backend") != backend:
        reasons.append(f"summary is for backend {summary.get('backend')!r}, not {backend!r}")
    if summary.get("scoreboardVersion") != scoreboard["scoreboardVersion"]:
        reasons.append("summary was produced for a different scoreboard version")
    if summary.get("functionalOnly") is not False:
        reasons.append("summary is functional-only (overridden samples or a row subset)")
    host = next((entry for entry in scoreboard.get("backends", []) if entry.get("backend") == backend), None)
    if host is not None and summary.get("capabilityMode") != host.get("capabilityMode"):
        reasons.append(f"summary capability mode {summary.get('capabilityMode')!r} is not the declared "
                       f"{host.get('capabilityMode')!r} for {backend}")
    rows = {row.get("rowId"): row for row in summary.get("rows", [])}
    for required in scoreboard["rows"]:
        row = rows.get(required["rowId"])
        if row is None or not row.get("workloads"):
            reasons.append(f"{required['rowId']}: unmeasured")
            continue
        entries = {entry.get("workloadId"): entry for entry in row["workloads"]}
        for workload_id in required["workloadIds"]:
            entry = entries.get(workload_id)
            if entry is None:
                reasons.append(f"{required['rowId']}/{workload_id}: unmeasured")
            elif entry.get("status") in ("not-comparable-on-host", "dawn-limitation"):
                detail = (entry.get("reasons") or ["no reason recorded"])[0]
                reasons.append(f"{required['rowId']}/{workload_id}: {entry.get('status')}: {detail}")
            elif entry.get("status") != "comparable":
                reasons.append(f"{required['rowId']}/{workload_id}: {entry.get('status')}")
            else:
                reason = _report_comparable(entry)
                if reason:
                    reasons.append(f"{required['rowId']}/{workload_id}: {reason}")
    return reasons


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    scoreboard = json.loads(args.scoreboard.read_text(encoding="utf-8"))
    summary = json.loads(args.summary.read_text(encoding="utf-8"))
    reasons = blocking_reasons(scoreboard, summary, args.backend)
    if reasons:
        print(f"release scoreboard BLOCKED for {args.backend}:")
        for reason in reasons:
            print(f"  - {reason}")
        return 1
    print(f"release scoreboard admissible for {args.backend}: every required row is comparable")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
