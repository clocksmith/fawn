"""Read workspace evidence without changing recorded scope or measurements."""

from __future__ import annotations

from typing import Any

from bench.inventory import model as reports
from bench.inventory.sources import Source, metric, assign_machine


def workspace(source: Source) -> list[dict[str, Any]]:
    payload = source.payload
    rows = []
    for i, observed in enumerate(payload.get("rows", [])):
        scenario = (
            observed.get("scenarioId")
            or observed.get("caseId")
            or observed.get("id")
            or "scenario:" + str(i)
        )
        row = source.row(scenario, "scenario:" + str(i), "execution")
        identity = payload.get("identity", {})
        assign_machine(row, identity.get("os", {}), "identity.os")
        row["implementation"].update(
            name=observed.get("arm", observed.get("side", "workspace")),
            sourceCommit=identity.get("git", {}).get("commit"),
        )
        service_hash = identity.get("serviceSource", {}).get("sha256")
        if service_hash:
            row["implementation"]["componentHashes"]["serviceSource"] = (
                service_hash
            )
        for side, name in [
            ("implementation", "treatment"),
            ("comparator", "control"),
        ]:
            browsers = payload.get("browsers", {}).get(name, [])
            if browsers:
                row[side]["binaryHash"] = browsers[0].get("sha256")
        row["comparator"]["name"] = "stock-automation"
        row["workloadContractHash"] = identity.get("comparisonPolicy", {}).get(
            "sha256"
        )
        row["executedAt"] = reports.execution_times(observed)["executedAt"]
        row["executionTimeSource"] = (
            source.registration["path"] + "#rows/" + str(i)
            if row["executedAt"]
            else None
        )
        row["executionStatus"] = (
            "succeeded"
            if observed.get("taskSuccess") is True
            else "failed"
            if observed.get("taskSuccess") is False or observed.get("error")
            else "unknown"
        )
        if isinstance(observed.get("taskSuccess"), bool):
            row["correctness"] = {
                "status": "passed" if observed["taskSuccess"] else "failed",
                "evidence": source.registration["path"]
                + "#rows/"
                + str(i)
                + "/taskSuccess",
            }
        for key, unit in [
            ("latencyMs", "ms"),
            ("observationBytes", "bytes"),
            ("incorrectActions", "count"),
            ("taskSuccess", "boolean"),
            ("recovered", "boolean"),
            ("constraintViolatingMutations", "count"),
            ("mutationsDuringHumanControl", "count"),
            ("humanReviews", "count"),
            ("duplicateEffects", "count"),
        ]:
            if observed.get(key) is not None:
                row["metrics"].append(
                    metric(
                        key,
                        unit,
                        observed[key],
                        scope="single behavioral scenario",
                        count=1,
                    )
                )
        row["verdict"] = row["executionStatus"]
        row["limitations"].append(
            "Workspace behavior and latency cannot establish native runtime or browser rendering qualification."
        )
        rows.append(row)
    summary = source.row(source.registration["familyId"], "report-summary")
    summary["verdict"] = "behavioral-status-only"
    advantage = payload.get("productAdvantage", {})
    if isinstance(advantage.get("established"), bool):
        summary["verdict"] = (
            "fixture-advantage-established"
            if advantage["established"]
            else "fixture-advantage-not-established"
        )
        summary["limitations"] += [
            "Does not establish " + item
            for item in advantage.get("doesNotEstablish", [])
        ]
    summary["limitations"].append(
        "Behavioral summary does not supply execution dates or native-runtime qualification."
    )
    rows.append(summary)
    for i, requirement in enumerate(payload.get("requirements", [])):
        row = source.row(
            requirement.get("scenario", "requirement:" + str(i)),
            "requirement:" + str(i),
        )
        row["verdict"] = "requirement-" + requirement["status"]
        row["acceptance"] = {
            "status": "passed" if requirement["status"] == "met" else "failed",
            "evidence": source.registration["path"] + "#requirements/" + str(i),
        }
        improvement = requirement.get("improvement", {})
        name = improvement.get("measure", "improvement")
        unit = (
            "ms"
            if name.endswith("Ms")
            else "bytes"
            if name.endswith("Bytes")
            else "count"
        )
        row["metrics"].append(
            metric(
                name,
                unit,
                improvement.get("treatment"),
                improvement.get("control"),
                improvement.get("ratio"),
                outcome=row["verdict"],
                scope="fixture requirement",
            )
        )
        row["limitations"].append(
            "Recorded fixture requirement, not release or native-runtime qualification."
        )
        rows.append(row)
    return rows
