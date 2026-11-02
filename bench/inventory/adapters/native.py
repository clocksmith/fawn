"""Read native evidence without changing recorded scope or measurements."""

from __future__ import annotations

from typing import Any

from bench.inventory import model as reports
from bench.inventory.sources import Source, file_hash, metric
from bench.tools import native_catalog_report as catalog_classifier


def native_catalog(source: Source) -> list[dict[str, Any]]:
    payload = source.payload
    contract = payload["contract"]
    backend = payload["backend"]
    policy_ref = source.policy["catalogClassification"]
    policy_path = source.root / policy_ref["path"]
    if file_hash(policy_path) != policy_ref["sha256"]:
        raise ValueError("Catalog classification policy checksum mismatch")
    classification = catalog_classifier.load_policy(policy_path)
    if classification.get("id") != policy_ref["id"]:
        raise ValueError("Catalog classification policy identity mismatch")
    rows = []
    for workload in payload["workloads"]:
        row = source.row(workload["id"])
        if workload["kind"] == "compilation":
            row["section"] = "compiler"
            row["comparisonType"] = "compiler-provider"
        row["backend"] = (
            "metal"
            if backend == "apple-metal"
            else ("d3d12" if backend == "windows-d3d12" else "vulkan")
        )
        row["machine"]["os"] = (
            "darwin"
            if backend == "apple-metal"
            else ("win32" if backend.startswith("windows-") else "linux")
        )
        row["workloadContractHash"] = payload.get("contractSha256")
        row["contractFrozenAt"] = reports.execution_times(contract)[
            "contractFrozenAt"
        ]
        row["implementation"].update(
            name="doe",
            sourceCommit=contract.get("gitCommit"),
            binaryHash=contract.get("binaries", {}).get("doeRuntimeSha256"),
        )
        row["comparator"].update(
            name="dawn",
            binaryHash=contract.get("binaries", {}).get("dawnRuntimeSha256"),
        )
        row["implementation"]["componentHashes"] = {
            k: v
            for k, v in contract.get("binaries", {}).items()
            if k in ("doeEmitterSha256",) and v
        }
        row["executionStatus"] = (
            "succeeded" if workload["supported"] else "failed"
        )
        row["limitations"] += [
            "Only retained aggregate statistics; individual execution samples are unavailable.",
            "Duration-based 3/5-pair exceptions and command-repeat1 cold/warm aliasing limit interpretation; see REANALYSIS.md.",
            "The catalog policy classifies losses only; all other rows are not automatically wins.",
        ]
        if not workload["supported"]:
            row["limitations"].append(
                "Doe: "
                + workload.get("doeStatus", "unknown")
                + "; Dawn: "
                + workload.get("dawnStatus", "unknown")
            )
            row["verdict"] = "failed-or-uncompared"
        else:
            for name, comparison in workload["comparisons"][
                classification["comparison"]
            ].items():
                doe = workload["arms"]["doe"][name]
                dawn = workload["arms"]["dawn"][name]
                outcome = (
                    "loss"
                    if catalog_classifier.is_loss(
                        name, comparison, doe, classification
                    )
                    else "not-classified-as-loss"
                )
                row["metrics"].append(
                    metric(
                        name,
                        "MiB" if name == "max_rss_mib" else "ms",
                        doe["p50"],
                        dawn["p50"],
                        comparison["p50"],
                        count=workload["pairs"],
                        ci=comparison["ci95"],
                        outcome=outcome,
                        policy=policy_ref,
                    )
                )
            row["verdict"] = "diagnostic"
        rows.append(row)
    return rows
