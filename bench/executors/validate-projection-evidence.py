"""Validate retained projection custody and recompute the experiment decision.

Repo-only diagnostic admission; this does not promote performance claims.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import statistics
import struct
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from bench.lib.compute_program_gpu_activity import reject_activity


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def validate_matrix(report: dict) -> None:
    require(report["performanceClaim"] is False, "Matrix is diagnostic")
    pairs = [(row["compiler"], row["runtime"]) for row in report["arms"]]
    require(Counter(pairs) == Counter((compiler, runtime) for compiler in ["doe", "tint"]
                                    for runtime in ["doe", "dawn"]), "Four distinct matrix arms required")
    for arm in report["arms"]:
        sequences = arm["sequences"]
        require(Counter(row["sequence"] for row in sequences) == Counter(["portable", "packed"]),
                "Both frozen sequences required")
        for row in sequences:
            if row["status"] != "passed":
                require(row["returnCode"] != 0, "Failure cannot hide a successful execution")
                if row["status"] == "unsupported":
                    require(arm["compiler"] == "tint" and arm["runtime"] == "dawn"
                            and row["sequence"] == "packed" and row["unsupportedReason"],
                            "Unsupported must name the actual integer-dot reader gap")
                continue
            observations = row["observations"]
            require(row["returnCode"] == 0, "Passing execution must exit successfully")
            require(any(item.get("kind") == "loadedLibrary" and item["observed"] for item in observations),
                    "Actual library custody required")
            require(any(item.get("kind") == "cleanup" and item["passed"] for item in observations),
                    "Successful owned cleanup required")
            samples = [item for item in observations if item["kind"] == "sample"]
            require([item["iteration"] for item in samples] == list(range(-report["sampling"]["warmup"],
                    report["sampling"]["iterations"])), "Complete frozen sample accounting required")
            require(all(item["exact"] and item["dispatches"] == 4 for item in samples), "Exact complete projection required")
            require(row["deviceCapabilities"] and all(item["result"] == 0 and item["vendorId"] == 0x8086
                    for item in row["deviceCapabilities"]), "Physical Intel device creation required")
            compiled = {item["stage"]: item for item in report["compilations"] if item["compiler"] == arm["compiler"]}
            expected = Counter(compiled[stage]["spirvSha256"] for stage in ["quantize", row["sequence"] + "-dot",
                                                                         "activation-rescale", "weight-rescale"])
            for stage in compiled.values():
                require(stage["returnCode"] == 0 and stage["validation"]["returnCode"] == 0,
                        "Compiled artifacts must validate independently")
            captures = row["vulkanCustody"]
            direct = bool(captures) and all(item["retained"] and item["result"] == 0 for item in captures)
            direct &= Counter(item["sha256"] for item in captures) == expected
            require(row["compilerProductPreserved"] == direct, "SPIR-V byte custody verdict differs")
        qualified = all(row["status"] == "passed" and row["compilerProductPreserved"] for row in sequences)
        require(arm["directCompilerReplacementQualified"] == qualified, "Compiler replacement requires every sequence")


def validate_cohort(report: dict, policy: dict, policy_bytes: bytes) -> None:
    require(report["policySha256"] == digest(policy_bytes), "Frozen policy identity differs")
    require(not report["fullGenerationPerformanceClaim"] and not policy["fullGenerationPerformanceClaim"],
            "Projection cannot qualify generation")
    rows = report["rows"]
    require(Counter((row["pair"], row["label"]) for row in rows) == Counter((pair, label)
            for pair in range(policy["processPairs"]) for label in ["portable", "accelerated"]),
            "Every predeclared child must remain")
    admitted = True
    for row in rows:
        observed = row["observations"]
        all_samples = [item for item in observed if item["kind"] == "sample"]
        require([item["iteration"] for item in all_samples] == list(range(-policy["warmup"], policy["iterations"])),
                "Warmup and timed sample accounting differs")
        require(any(item["kind"] == "adapter" and item["physicalIntelVulkan"] for item in observed), "Physical adapter required")
        require(any(item["kind"] == "loadedLibrary" and item["observed"] for item in observed), "Library custody required")
        require(any(item["kind"] == "cleanup" and item["passed"] for item in observed), "Owned cleanup required")
        samples = [item for item in all_samples if item["iteration"] >= 0]
        passed = row["returnCode"] == 0 and len(samples) == policy["iterations"] and all(item["exact"] for item in samples)
        passed &= row["gpuActivity"]["failure"] is None
        if passed:
            reject_activity(row["gpuActivity"]["snapshots"], report["target"]["pciDevice"])
        require(row["passed"] == passed, "Child admission differs")
        admitted &= passed
        if passed:
            require(row["medianMs"] == statistics.median(item["ms"] for item in samples), "Timing median differs")
            require(row["cpuMs"] == statistics.median(item["cpuMs"] for item in samples), "CPU median differs")
            require(row["preparationMs"] == next(item["ms"] for item in observed if item["kind"] == "preparation"), "Preparation differs")
            require(row["peakRssKiB"] == next(item["peakRssKiB"] for item in observed if item["kind"] == "processResources"), "RSS differs")
    require(report["allRunsAdmitted"] == admitted, "Cohort admission differs")
    if not admitted:
        require(not report["accepted"], "Failed cohort cannot qualify")
        return
    ratios = []
    metrics = ["medianMs", *policy["regressionMetrics"]]
    for pair in range(policy["processPairs"]):
        baseline = next(row for row in rows if row["pair"] == pair and row["label"] == "portable")
        candidate = next(row for row in rows if row["pair"] == pair and row["label"] == "accelerated")
        ratios.append({"pair": pair, **{metric: candidate[metric] / baseline[metric] for metric in metrics}})
    require(report["pairedRatios"] == ratios, "Paired ratios differ")
    medians = {metric: statistics.median(row[metric] for row in ratios) for metric in metrics}
    require(report["metrics"] == {metric: {"medianPairedRatio": value} for metric, value in medians.items()}, "Cohort metrics differ")
    accepted = medians["medianMs"] <= policy["maximumPrimaryRatio"] and all(
        medians[metric] <= policy["maximumRegressionRatio"] for metric in policy["regressionMetrics"])
    require(report["accepted"] == accepted, "Frozen acceptance decision differs")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    root = args.directory.resolve()
    manifest = json.loads((root / "manifest.json").read_text())
    for record in manifest["artifacts"]:
        path = root / record["path"]
        require(path.resolve().is_relative_to(root), "Artifact must stay within report")
        require(digest(path.read_bytes()) == record["sha256"], "Changed retained artifact: " + record["path"])
    matrix = json.loads((root / "matrix.json").read_text())
    validate_matrix(matrix)
    with tarfile.open(root / "raw-evidence.tar.gz", "r:gz") as archive:
        require(digest(archive.extractfile("model-quality/README.md").read()) == manifest["modelGate"]["sha256"],
                "Model gate source custody differs")
        for arm in matrix["arms"]:
            for row in arm["sequences"]:
                stem = f"matrix-final/{arm['compiler']}-{arm['runtime']}-{row['sequence']}.custody/"
                for capture in row["vulkanCustody"]:
                    data = archive.extractfile(stem + capture["artifact"]).read()
                    require(digest(data) == capture["sha256"], "Driver shader bytes differ")
                    words = struct.unpack("<" + "I" * (len(data) // 4), data)
                    index, needs_dot = 5, False
                    while index < len(words):
                        count, opcode = words[index] >> 16, words[index] & 0xffff
                        require(count > 0 and index + count <= len(words), "Invalid captured instruction")
                        needs_dot |= opcode == 17 and count == 2 and words[index + 1] == 6019
                        index += count
                    if needs_dot:
                        require(all(item["integerDotAvailable"] and item["integerDotEnabled"]
                                for item in row["deviceCapabilities"]), "Integer dot must be enabled on the actual device")
    policy_bytes = (root / "policy.json").read_bytes()
    validate_cohort(json.loads((root / "cohort.json").read_text()), json.loads(policy_bytes), policy_bytes)
    print("Projection evidence hashes, four-arm custody and frozen rejection verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
