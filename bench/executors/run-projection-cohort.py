"""Run a frozen alternating native projection cohort without shader diagnostics."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

from bench.lib.compute_program_gpu_activity import detect_target, read_snapshot, reject_activity


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["policy", "executor", "library", "output"]:
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    policy_path = args.policy.resolve()
    policy = json.loads(policy_path.read_text(encoding="utf-8"))
    if digest(args.library) != policy["librarySha256"] or digest(args.executor) != policy["executorSha256"]:
        raise ValueError("Executor/library differs from the frozen policy")
    plans = {}
    for label, key in [("portable", "baseline"), ("accelerated", "candidate")]:
        path = policy_path.parent / policy[key]["plan"]
        if digest(path) != policy[key]["sha256"]:
            raise ValueError("Projection plan differs from the frozen policy")
        plans[label] = path.resolve()
    target = detect_target()
    if target["vendorId"] != 0x8086:
        raise ValueError("This cohort requires the explicit Intel/Vulkan target")
    output = args.output.resolve()
    output.mkdir()
    env = dict(os.environ)
    for name in ["VK_INSTANCE_LAYERS", "VK_LAYER_PATH", "DOE_VULKAN_CUSTODY_DIR",
                 "DOE_PROGRAM_IDENTITY_TRACE_PATH", "INTEL_DEBUG", "MESA_SHADER_CACHE_DISABLE"]:
        env.pop(name, None)
    env["VK_ICD_FILENAMES"] = "/usr/share/vulkan/icd.d/intel_icd.x86_64.json"
    report = {"schemaVersion": 1, "classification": "packed-projection-experiment",
              "policySha256": digest(policy_path), "target": target,
              "rows": [], "pairedRatios": [], "metrics": {}, "accepted": False,
              "fullGenerationPerformanceClaim": False}
    for pair in range(policy["processPairs"]):
        order = ["portable", "accelerated"] if pair % 2 == 0 else ["accelerated", "portable"]
        for label in order:
            command = [str(args.executor.resolve()), str(args.library.resolve()),
                       str(plans[label]), str(policy["iterations"]), str(policy["warmup"])]
            before = read_snapshot(target)
            result = subprocess.run(command, capture_output=True, env=env, timeout=120)
            after = read_snapshot(target)
            stem = f"pair-{pair:02d}-{label}"
            (output / (stem + ".stdout.log")).write_bytes(result.stdout)
            (output / (stem + ".stderr.log")).write_bytes(result.stderr)
            observed = [json.loads(line) for line in result.stdout.decode().splitlines() if line.startswith("{")]
            samples = [row for row in observed if row["kind"] == "sample" and row["iteration"] >= 0]
            gpu_failure = None
            try:
                reject_activity([before, after], target["pciDevice"])
            except ValueError as error:
                gpu_failure = str(error)
            row = {"pair": pair, "label": label, "command": command,
                   "returnCode": result.returncode, "observations": observed,
                   "gpuActivity": {"snapshots": [before, after], "failure": gpu_failure},
                   "passed": result.returncode == 0 and len(samples) == policy["iterations"]
                     and all(sample["exact"] for sample in samples) and gpu_failure is None}
            if row["passed"]:
                row["medianMs"] = statistics.median(sample["ms"] for sample in samples)
                row["cpuMs"] = statistics.median(sample["cpuMs"] for sample in samples)
                row["preparationMs"] = next(item["ms"] for item in observed if item["kind"] == "preparation")
                row["peakRssKiB"] = next(item["peakRssKiB"] for item in observed if item["kind"] == "processResources")
            report["rows"].append(row)
            write_json(output / (stem + ".json"), row)
    all_passed = all(row["passed"] for row in report["rows"])
    if all_passed:
        for pair in range(policy["processPairs"]):
            baseline = next(row for row in report["rows"] if row["pair"] == pair and row["label"] == "portable")
            candidate = next(row for row in report["rows"] if row["pair"] == pair and row["label"] == "accelerated")
            report["pairedRatios"].append({"pair": pair, **{
                metric: candidate[metric] / baseline[metric] for metric in ["medianMs", *policy["regressionMetrics"]]}})
        for metric in ["medianMs", *policy["regressionMetrics"]]:
            report["metrics"][metric] = {"medianPairedRatio": statistics.median(row[metric] for row in report["pairedRatios"])}
        report["accepted"] = report["metrics"]["medianMs"]["medianPairedRatio"] <= policy["maximumPrimaryRatio"] and all(
            report["metrics"][metric]["medianPairedRatio"] <= policy["maximumRegressionRatio"] for metric in policy["regressionMetrics"])
    report["allRunsAdmitted"] = all_passed
    write_json(output / "results.json", report)
    print(json.dumps({"allRunsAdmitted": all_passed, "metrics": report["metrics"], "accepted": report["accepted"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
