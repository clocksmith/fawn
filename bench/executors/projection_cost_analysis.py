"""Validate and summarize every process in a projection-cost diagnostic cohort."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import statistics

from bench.executors.projection_cost import digest, load, save, summarize, validate


def analyze(folder: Path, policy_path: Path) -> dict:
    """Keep A/A variation, profiler perturbation and compiler effects separate."""
    report = load(folder / "report.json"); validate(report)
    policy = load(policy_path); validate(policy)
    if not report["passed"] or report["inputs"]["policy"]["sha256"] != digest(policy_path):
        raise ValueError("Incomplete cohort or changed sampling policy")
    labels = set()
    indexed = {}
    for row in report["runs"]:
        if row["label"] in labels or row["returnCode"] != 0:
            raise ValueError("Duplicated process or failed execution")
        labels.add(row["label"])
        path = folder / row["label"] / "stdout.jsonl"
        if digest(path) != row["logSha256"]:
            raise ValueError("Changed raw process log")
        rows = [json.loads(line) for line in path.read_text().splitlines() if line.startswith("{")]
        checked = summarize(rows, policy["iterations"], policy["warmup"],
                            row["mode"], row["transfer"], 12936)
        if checked != row["summary"]:
            raise ValueError("Summary differs from raw observations")
        if not row["custody"]:
            repeat, compiler, sequence, transfer, treatment = row["label"].split("-", 4)
            if (compiler, sequence, transfer) != (row["compiler"], row["sequence"], row["transfer"]):
                raise ValueError("Process identity differs from its label")
            indexed[int(repeat[1:]), compiler, sequence, transfer, treatment] = checked
    expected = {f"custody-{c}-{s}" for c in ["tint", "doe"] for s in ["portable", "packed"]}
    expected.update(f"r{r}-{c}-{s}-{t}-{m}" for r in range(policy["repeats"])
                    for c in ["tint", "doe"] for s in ["portable", "packed"]
                    for t in ["resident", "verification"]
                    for m in ["none-a", "none-b", "host", "gpu"])
    if labels != expected:
        raise ValueError("Cohort omits or adds frozen process identities")
    result = {"schemaVersion": 1, "kind": "projection-cost-analysis",
              "performanceClaim": False, "reportSha256": digest(folder / "report.json"),
              "policySha256": digest(policy_path), "comparisons": [], "observations": []}

    def compare(name: str, numerator: tuple, denominator: tuple) -> None:
        ratios = [indexed[(r,) + numerator]["medianMs"] / indexed[(r,) + denominator]["medianMs"]
                  for r in range(policy["repeats"])]
        result["comparisons"].append({"name": name, "ratios": ratios,
            "medianRatio": statistics.median(ratios), "minimumRatio": min(ratios),
            "maximumRatio": max(ratios)})

    for compiler in ["tint", "doe"]:
        for sequence in ["portable", "packed"]:
            for transfer in ["resident", "verification"]:
                arm = (compiler, sequence, transfer)
                prefix = "/".join(arm)
                for treatment in ["none-b", "host", "gpu"]:
                    compare(prefix + "/" + treatment + "-over-none-a", arm + (treatment,), arm + ("none-a",))
                rows = [indexed[(r,) + arm + ("none-a",)] for r in range(policy["repeats"])]
                hosts = [indexed[(r,) + arm + ("host",)] for r in range(policy["repeats"])]
                gpus = [indexed[(r,) + arm + ("gpu",)] for r in range(policy["repeats"])]
                result["observations"].append({"compiler": compiler, "sequence": sequence,
                    "transfer": transfer, "medianWallMs": statistics.median(r["medianMs"] for r in rows),
                    "medianCpuMs": statistics.median(r["medianCpuMs"] for r in rows),
                    "hostPhases": {k: statistics.median(r["phases"][k] for r in hosts) for k in hosts[0]["phases"]},
                    "gpuStageNs": [statistics.median(r["gpuStageNs"][i] for r in gpus) for i in range(4)]})
            compare(f"{compiler}/{sequence}/resident-over-verification",
                    (compiler, sequence, "resident", "none-a"),
                    (compiler, sequence, "verification", "none-a"))
        for transfer in ["resident", "verification"]:
            compare(f"{compiler}/{transfer}/packed-over-portable",
                    (compiler, "packed", transfer, "none-a"),
                    (compiler, "portable", transfer, "none-a"))
    for sequence in ["portable", "packed"]:
        for transfer in ["resident", "verification"]:
            compare(f"{sequence}/{transfer}/doe-over-tint",
                    ("doe", sequence, transfer, "none-a"),
                    ("tint", sequence, transfer, "none-a"))
    validate(result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cohort", type=Path, help="Complete retained process folder")
    parser.add_argument("policy", type=Path, help="Frozen sampling policy")
    parser.add_argument("output", type=Path, help="New diagnostic analysis JSON")
    args = parser.parse_args()
    save(args.output, analyze(args.cohort, args.policy))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
