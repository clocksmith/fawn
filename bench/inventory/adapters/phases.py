"""Read phases evidence without changing recorded scope or measurements."""

from __future__ import annotations

import gzip
import json
from typing import Any

from bench.inventory import model as reports
from bench.inventory.sources import Source, metric, phase_unit, assign_machine


def bind_phase(
    row: dict[str, Any], source: Source, provider: str, control: str
) -> None:
    contract = source.data("contract") or {}
    row["workloadContractHash"] = (source.reference("workload-contract") or source.reference("contract") or {}).get(
        "sha256"
    )
    row["contractFrozenAt"] = reports.execution_times(contract)[
        "contractFrozenAt"
    ]
    row["backend"] = contract.get("backend") or (
        "metal" if "metal" in source.registration["familyId"] else "vulkan")
    row["implementation"]["name"] = provider
    row["comparator"]["name"] = control
    if provider == "doe":
        row["implementation"]["sourceCommit"] = contract.get("doeCommit")
    if control == "dawn":
        row["comparator"]["sourceCommit"] = contract.get("dawnCommitAsRecorded")
    binaries = contract.get("binaryManifest", {})
    if provider == "doe":
        row["implementation"]["binaryHash"] = binaries.get(
            "libraries/libwebgpu_doe_c.dylib"
        )
        compiler = binaries.get("libraries/libdoe_compiler.dylib")
        if compiler:
            row["implementation"]["componentHashes"]["compiler"] = compiler
    row["comparator"]["binaryHash"] = (
        binaries.get("libraries/libwebgpu_dawn.dylib")
        if control == "dawn"
        else None
    )
    host = contract.get("host", {})
    if isinstance(host, dict):
        assign_machine(row, host, "contract.host")
    execution = source.data("execution")
    if isinstance(execution, str):
        stamp = reports.parse_utc_iso(execution.strip())
        if stamp:
            row["executedAt"] = reports.iso_utc(stamp)
            row["executionTimeSource"] = source.reference("execution")["path"]
    elif isinstance(execution, dict):
        # Gate snapshots explicitly bracket execution; these are not contract clocks.
        stamp = execution.get("after", {}).get("utc")
        parsed = reports.parse_utc_iso(stamp)
        if parsed:
            row["executedAt"] = reports.iso_utc(parsed)
            row["executionTimeSource"] = (
                source.reference("execution")["path"] + "#/after/utc"
            )
    row["limitations"].append(
        "No qualification follows from a phase summary or a valid host window."
    )


def phase_cohort(source: Source) -> list[dict[str, Any]]:
    rows = []
    executions: dict[tuple[str, str], list[dict[str, Any]]] = {}
    verdicts = source.data("verdicts") or {}
    for workload, section in source.payload["workloads"].items():
        for comparison, paired in section["paired"].items():
            provider, control = comparison.split("/")
            row = source.row(workload, comparison)
            bind_phase(row, source, provider, control)
            row["executionStatus"] = (
                "succeeded"
                if not source.payload.get("failedProcesses")
                else "partially-failed"
            )
            row["verdict"] = "diagnostic"
            for name, ratio in paired["ratio"].items():
                observed = (
                    verdicts.get("results", {}).get(workload, {}).get(name, {})
                )
                # Supplemental verdicts apply only to their declared provider pair.
                outcome = (
                    observed.get("classification", "unknown")
                    if comparison == "doe/dawn"
                    else "unknown"
                )
                row["metrics"].append(
                    metric(
                        name,
                        phase_unit(name),
                        section["providers"][provider]["median"].get(name),
                        section["providers"][control]["median"].get(name),
                        ratio["median"],
                        count=ratio.get("n"),
                        ci=ratio.get("ci95"),
                        outcome=outcome,
                    )
                )
            row["limitations"].append(
                "Aggregate row; raw process observations, when retained, are separate rows."
            )
            rows.append(row)
    raw_refs = [
        r
        for r in source.references
        if r["role"] == "raw" or r["role"].startswith("raw:")
    ]
    for raw_ref in raw_refs:
        if raw_ref["availability"] != "retained":
            continue
        from bench.tools.dropin_phase_cohort import process_metrics

        path = source.root / raw_ref["path"]
        stream = (
            gzip.open(path, "rt", encoding="utf-8")
            if path.suffix == ".gz"
            else path.open(encoding="utf-8")
        )
        with stream:
            for i, line in enumerate(stream):
                payload = json.loads(line)
                provider = payload["provider"]
                row = source.row(
                    payload["workload"],
                    raw_ref["path"] + ":process:" + str(i),
                    "execution",
                )
                row["evidence"] = [
                    r
                    for r in row["evidence"]
                    if not r["role"].startswith("raw:") or r == raw_ref
                ]
                bind_phase(row, source, provider, "unpaired")
                contract = source.data("contract") or {}
                binary = payload.get("binary", "").replace(chr(92), "/")
                matches = [
                    (k, v)
                    for k, v in contract.get("hashes", {}).items()
                    if binary.endswith(k)
                ]
                if len(matches) == 1:
                    row["implementation"]["binaryHash"] = matches[0][1]
                result = payload.get("result", {})
                assign_machine(
                    row,
                    result.get("host", contract.get("host", {})),
                    "raw.result.host-or-contract.host",
                    {"device": result.get("adapterDevice")},
                )
                row["executionStatus"] = (
                    "succeeded"
                    if payload.get("returnCode") == 0
                    and result.get("status") == "ok"
                    else "failed"
                )
                row["verdict"] = (
                    "observed"
                    if row["executionStatus"] == "succeeded"
                    else "failed"
                )
                row["correctness"] = {
                    "status": "passed"
                    if result.get("oracleChecked") == result.get("iterations")
                    and result.get("iterations", 0) > 0
                    else "unknown",
                    "evidence": raw_ref["path"] + "#line=" + str(i + 1),
                }
                if row["executionStatus"] == "succeeded":
                    row["metrics"] = [
                        metric(k, phase_unit(k), v, scope="process", count=1)
                        for k, v in process_metrics(payload).items()
                    ]
                else:
                    row["limitations"].append(
                        "Process failed: " + str(payload.get("stderr", result))
                    )
                # A whole-window timestamp is not a per-process execution timestamp.
                row["executedAt"] = reports.execution_times(result)[
                    "executedAt"
                ]
                row["executionTimeSource"] = (
                    raw_ref["path"] + "#line=" + str(i + 1)
                    if row["executedAt"]
                    else None
                )
                reports.apply_provenance(row, payload)
                executions.setdefault((payload["workload"], provider), []).append({
                    "provenance": payload.get("provenance"), "correctness": row["correctness"],
                    "rawEvidence": raw_ref["path"],
                    "executionTimeSource": raw_ref["path"] + "#line=" + str(i + 1) + "/provenance/executedAt",
                })
                rows.append(row)
    for row in rows:
        if row["observationLevel"] == "aggregate":
            observations = {
                side: executions.get((row["workloadId"], row[side]["name"]), [])
                for side in ("implementation", "comparator")
            }
            reports.apply_comparison_provenance(row, observations)
            selected = [record for values in observations.values() for record in values]
            if all(observations.values()) and all(record["correctness"]["status"] == "passed" for record in selected):
                row["correctness"] = {"status": "passed", "evidence": ", ".join(
                    sorted({record["rawEvidence"] for record in selected}))
                    + "#all selected workload/provider oracleChecked equals iterations"}
    return rows
