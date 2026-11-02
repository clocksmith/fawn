"""Reproduce catalog classifications from retained aggregate summaries.

The supplied policy owns every threshold. This tool neither recomputes missing
raw-sample statistics nor promotes diagnostic results into performance claims.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
from typing import Any


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def finite_number(value: Any, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{field}: expected finite number, got {value!r}")
    if not math.isfinite(value):
        raise ValueError(f"{field}: expected finite number, got {value!r}")
    return float(value)


def load_policy(path: Path) -> dict[str, Any]:
    policy = read_json(path)
    if policy.get("schemaVersion") != 1:
        raise ValueError(f"{path}: unsupported classification policy version")
    for key in ("ratioExclusiveMinimum", "confidenceLowerExclusiveMinimum"):
        finite_number(policy[key], key)
    for key in ("milliseconds", "max_rss_mib"):
        value = finite_number(policy["doeMedianMinimum"][key], key)
        if value < 0:
            raise ValueError(f"{path}: negative median threshold for {key}")
    if not policy["operationMetrics"] or not policy["headlineMetric"]:
        raise ValueError(f"{path}: missing operation/headline metric")
    return policy


def is_loss(metric: str, comparison: dict[str, Any],
            doe: dict[str, Any], policy: dict[str, Any]) -> bool:
    if metric != "max_rss_mib" and not metric.endswith("_ms"):
        raise ValueError(f"{metric}: no declared unit/median floor")
    ratio = finite_number(comparison["p50"], f"{metric}.ratio")
    lower = finite_number(comparison["ci95"][0], f"{metric}.ci95.lower")
    median = finite_number(doe["p50"], f"{metric}.doe.median")
    unit = "max_rss_mib" if metric == "max_rss_mib" else "milliseconds"
    return (ratio > policy["ratioExclusiveMinimum"]
            and lower > policy["confidenceLowerExclusiveMinimum"]
            and median >= policy["doeMedianMinimum"][unit])


def classify_catalogs(directory: Path, policy_path: Path) -> dict[str, Any]:
    policy = load_policy(policy_path)
    files = sorted(directory.glob("catalog_*.json"))
    if not files:
        raise ValueError(f"{directory}: no catalog_*.json summaries")
    rows: list[dict[str, Any]] = []
    backends: list[dict[str, Any]] = []
    inputs = []
    seen_backends: set[str] = set()
    for path in files:
        catalog = read_json(path)
        backend = catalog["backend"]
        if backend in seen_backends:
            raise ValueError(f"{path}: duplicate backend {backend}")
        seen_backends.add(backend)
        inputs.append({"path": path.name, "sha256": file_hash(path),
                       "measuredSource": catalog["contract"]["gitCommit"]})
        pair_counts: Counter[int] = Counter()
        phase_counts: Counter[str] = Counter()
        seen_ids: set[str] = set()
        supported = 0
        for workload in catalog["workloads"]:
            workload_id = workload["id"]
            if workload_id in seen_ids:
                raise ValueError(f"{path}: duplicate workload {workload_id}")
            seen_ids.add(workload_id)
            if not workload["supported"]:
                continue
            supported += 1
            pairs = workload["pairs"]
            if isinstance(pairs, bool) or not isinstance(pairs, int) or pairs <= 0:
                raise ValueError(f"{path}/{workload_id}: invalid pair count {pairs}")
            pair_counts[pairs] += 1
            for metric, comparison in workload["comparisons"][policy["comparison"]].items():
                doe = workload["arms"]["doe"][metric]
                if not is_loss(metric, comparison, doe, policy):
                    continue
                dawn = workload["arms"]["dawn"][metric]
                phase_counts[metric] += 1
                rows.append({
                    "backend": backend, "workload": workload_id, "phase": metric,
                    "ratio_p50": comparison["p50"],
                    "ratio_p95": comparison["p95"], "ci95": comparison["ci95"],
                    "doe_p50": doe["p50"], "doe_p95": doe["p95"],
                    "dawn_p50": dawn["p50"], "dawn_p95": dawn["p95"],
                    "aa_ratio_p50": workload["comparisons"]["dawn-aa/dawn"][metric]["p50"],
                    "pairs": pairs,
                })
        if supported != catalog["supportedCount"]:
            raise ValueError(f"{path}: supportedCount does not match workload rows")
        backends.append({
            "backend": backend, "supported": supported,
            "unsupported": catalog["unsupportedCount"],
            "measuredSource": catalog["contract"]["gitCommit"],
            "pairCounts": dict(sorted(pair_counts.items())),
            "phaseCounts": dict(sorted(phase_counts.items())),
            "headlineLosses": phase_counts[policy["headlineMetric"]],
            "operationLosses": sum(phase_counts[m] for m in policy["operationMetrics"]),
            "allPhaseLosses": sum(phase_counts.values()),
        })
    rows.sort(key=lambda row: (-row["ratio_p50"], row["backend"],
                               row["workload"], row["phase"]))
    operation_rows = [r for r in rows if r["phase"] in policy["operationMetrics"]]
    return {
        "schemaVersion": 1, "policy": policy,
        "policySha256": file_hash(policy_path), "inputs": inputs,
        "interpretation": "Diagnostic classification of retained aggregate statistics; not current HEAD or independently reconstructed samples.",
        "backends": backends, "totalPhaseLosses": len(rows),
        "totalOperationLosses": len(operation_rows),
        "headlineLosses": sum(r["phase"] == policy["headlineMetric"] for r in rows),
        "operationLosses": operation_rows, "allPhaseLosses": rows,
    }


def render_report(report: dict[str, Any]) -> str:
    policy = report["policy"]
    floors = policy["doeMedianMinimum"]
    lines = [
        "# Reproducible native catalog classification", "",
        report["interpretation"], "",
        "Per-pair arrays and traces are not retained for this catalog. This "
        "report reclassifies stored medians and confidence intervals; it does "
        "not reconstruct their statistics or prove independent cold/warm "
        "measurements. See REANALYSIS.md and sampling-lineage.json in the "
        "catalog source directory for duration-based sample counts and "
        "the recovered runner's cold/warm aliasing limitation.", "",
        f"Policy `{policy['id']}`: ratio > {policy['ratioExclusiveMinimum']}, "
        f"95% CI lower bound > {policy['confidenceLowerExclusiveMinimum']}, "
        f"and Doe median >= {floors['milliseconds']} ms "
        f"(>= {floors['max_rss_mib']} MiB for max_rss_mib). "
        "The floor is an absolute Doe median, not the Doe-minus-Dawn gap.", "",
        f"Headline (`{policy['headlineMetric']}`): {report['headlineLosses']} losses. "
        f"Cold plus warm operation: {report['totalOperationLosses']}. "
        f"All phase/CPU/memory metrics: {report['totalPhaseLosses']}.", "",
        "| Backend | Measured source | Supported | Unsupported | Headline | Cold + warm operation | All metrics | Pairs: workload count |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for backend in report["backends"]:
        pairs = ", ".join(f"{n}: {count}" for n, count in backend["pairCounts"].items())
        lines.append(f"| {backend['backend']} | `{backend['measuredSource']}` | "
                     f"{backend['supported']} | {backend['unsupported']} | "
                     f"{backend['headlineLosses']} | {backend['operationLosses']} | "
                     f"{backend['allPhaseLosses']} | {pairs} |")
    lines.extend([
        "", "## Ranked diagnostic losses", "",
        "| Rank | Backend | Workload | Metric | Doe / Dawn | 95% CI | Doe median | Dawn median | Pairs |",
        "| ---: | --- | --- | --- | ---: | --- | ---: | ---: | ---: |",
    ])
    for rank, row in enumerate(report["allPhaseLosses"], 1):
        lines.append(f"| {rank} | {row['backend']} | `{row['workload']}` | "
                     f"{row['phase']} | {row['ratio_p50']:.9g} | "
                     f"[{row['ci95'][0]:.9g}, {row['ci95'][1]:.9g}] | "
                     f"{row['doe_p50']:.9g} | {row['dawn_p50']:.9g} | {row['pairs']} |")
    return "\n".join(lines) + "\n"


def check_retained(report: dict[str, Any], path: Path) -> None:
    retained = read_json(path)
    for field in ("operationLosses", "allPhaseLosses"):
        def keys(rows: list[dict[str, Any]]) -> set[tuple[str, str, str]]:
            return {(r["backend"], r["workload"], r["phase"]) for r in rows}
        expected = keys(report[field])
        actual = keys(retained[field])
        if expected != actual or len(actual) != len(retained[field]):
            raise ValueError(f"{path}/{field}: {len(expected - actual)} missing, "
                             f"{len(actual - expected)} extra or duplicate retained rows")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalog", type=Path, required=True,
                        help="Directory containing retained catalog_*.json files")
    parser.add_argument("--policy", type=Path, required=True,
                        help="Versioned classification policy JSON")
    parser.add_argument("--out", type=Path, required=True,
                        help="Output directory for classification.json and .md")
    parser.add_argument("--check", action="store_true",
                        help="Check existing derived outputs without changing files")
    parser.add_argument("--check-retained", type=Path,
                        help="Check classified row membership against retained ranking JSON")
    args = parser.parse_args(argv)
    report = classify_catalogs(args.catalog, args.policy)
    if args.check_retained:
        check_retained(report, args.check_retained)
    outputs = {
        "classification.json": json.dumps(report, indent=2, sort_keys=True) + "\n",
        "classification.md": render_report(report),
    }
    if args.check:
        for name, text in outputs.items():
            path = args.out / name
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                raise ValueError(f"{path}: derived classification is stale")
    else:
        args.out.mkdir(parents=True, exist_ok=True)
        for name, text in outputs.items():
            (args.out / name).write_text(text, encoding="utf-8")
    print(f"Catalog classification: {report['headlineLosses']} headline, "
          f"{report['totalOperationLosses']} operation, {report['totalPhaseLosses']} total")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
