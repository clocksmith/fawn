"""Read comparisons evidence without changing recorded scope or measurements."""

from __future__ import annotations

from typing import Any

from bench.inventory import model as reports
from bench.inventory.sources import Source, metric, assign_machine


def compare_report(source: Source) -> list[dict[str, Any]]:
    """Read existing legacy compare receipts without reauthorizing their claims."""
    payload = source.payload
    participants = payload.get("participants", {})
    left, right = participants.get("left", {}), participants.get("right", {})
    rows = []
    for i, observed in enumerate(payload.get("workloads", [])):
        row = source.row(str(observed.get("id", i)))
        assign_machine(
            row,
            left.get("hostIdentity", {}),
            "participants.left.hostIdentity",
            left.get("hostIdentity", {}).get("adapter"),
        )
        row["backend"] = left.get("hostIdentity", {}).get("api")
        for side, participant in [
            ("implementation", left),
            ("comparator", right),
        ]:
            runtime = participant.get("runtimeIdentity", {})
            row[side].update(
                name=participant.get("product"),
                sourceCommit=runtime.get("sourceCommit"),
                binaryHash=runtime.get("binarySha256"),
            )
            delegate = runtime.get("nativeDelegate", {})
            if delegate.get("librarySha256"):
                row[side]["componentHashes"]["provider"] = delegate[
                    "librarySha256"
                ]
        row["executionStatus"] = "unknown"
        row["verdict"] = "reported-" + str(
            payload.get("comparisonStatus", "diagnostic")
        )
        for suffix in ("p50", "p95", "p99"):
            a = observed.get("baseline", {}).get("stats", {}).get(suffix + "Ms")
            b = (
                observed.get("comparison", {})
                .get("stats", {})
                .get(suffix + "Ms")
            )
            if a is not None or b is not None:
                row["metrics"].append(
                    metric(
                        suffix,
                        "ms",
                        a,
                        b,
                        a / b if b and a is not None else None,
                        scope="legacy reported timing; see original comparability",
                    )
                )
        row["limitations"].append(
            "Historical claim/index labels are publication authority, not new qualification or current source evidence."
        )
        rows.append(row)
    return rows


def application(source: Source) -> list[dict[str, Any]]:
    """Expose retained application/compiler records without inventing admission."""
    payload = source.payload
    row = source.row(
        str(
            payload.get("module")
            or payload.get("reportId")
            or source.registration["familyId"]
        )
    )
    row["verdict"] = str(
        payload.get("disposition")
        or payload.get("outcome")
        or payload.get("classification")
        or "unknown"
    )
    if isinstance(payload.get("passed"), bool):
        row["acceptance"] = {
            "status": "passed" if payload["passed"] else "failed",
            "evidence": source.registration["path"] + "#passed",
        }
    equivalence = payload.get("equivalence", {})
    if equivalence.get("oracleStatus") in ("pass", "fail"):
        row["correctness"] = {
            "status": "passed"
            if equivalence["oracleStatus"] == "pass"
            else "failed",
            "evidence": source.registration["path"]
            + "#equivalence/oracleStatus",
        }
    row["limitations"] += payload.get("limitations", [])
    row["limitations"].append(
        "No execution identities, dates or samples are inferred from an application verdict or report directory name."
    )
    for key in ("primaryRatio", "lowerConfidenceRatio", "aaMedianRatio"):
        if isinstance(payload.get(key), (int, float)):
            row["metrics"].append(
                metric(
                    key,
                    "ratio",
                    payload[key],
                    scope="reported application decision",
                )
            )
    for name, value in payload.get("regressionRatios", {}).items():
        row["metrics"].append(
            metric(
                name, "ratio", ratio=value, scope="reported regression ratio"
            )
        )
    performance = payload.get("performance", {})
    for phase, values in performance.get("comparison", {}).items():
        for stat, value in values.items():
            baseline = performance.get("baseline", {}).get(phase, {}).get(stat)
            row["metrics"].append(
                metric(
                    phase + "." + stat,
                    performance.get("unit"),
                    value,
                    baseline,
                    value / baseline if baseline else None,
                    scope="reported application performance",
                )
            )
    rows = [row]
    for provider, data in payload.get("providers", {}).items():
        if not isinstance(data, dict):
            continue
        for arm, metrics in data.get("arms", {}).items():
            observed = source.row(provider + "/" + arm, provider + "/" + arm)
            observed["implementation"]["name"] = provider
            observed["verdict"] = row["verdict"]

            def add_stats(prefix: str, values: dict[str, Any]) -> None:
                for name, value in values.items():
                    if isinstance(value, dict) and "p50" in value:
                        unit = (
                            "bytes"
                            if name.endswith("Bytes")
                            else "ms"
                            if name.endswith("Ms")
                            else None
                        )
                        observed["metrics"].append(
                            metric(
                                prefix + name,
                                unit,
                                value["p50"],
                                count=len(value["samples"])
                                if "samples" in value
                                else None,
                                scope="retained " + arm + " aggregate",
                            )
                        )
                    elif isinstance(value, dict):
                        add_stats(prefix + name + ".", value)

            add_stats("", metrics)
            rows.append(observed)
        if data.get("timingsMs"):
            observed = source.row(provider, provider)
            observed["implementation"]["name"] = provider
            observed["verdict"] = row["verdict"]
            for phase, timings in data["timingsMs"].items():
                for name, stats in timings.items():
                    for stat, value in stats.items():
                        observed["metrics"].append(
                            metric(phase + "." + name + "." + stat, "ms", value)
                        )
            for name, values in data.get("controls", {}).items():
                observed["metrics"].append(
                    metric(
                        "control." + name,
                        "ratio",
                        ratio=values["ratio"],
                        ci=[values["lower"], values["upper"]],
                        count=len(values.get("pairedLogRatios", [])),
                        scope="same-provider control, not Doe versus Dawn",
                    )
                )
            rows.append(observed)
    for case_index, case in enumerate(payload.get("rows", [])):
        for arm, group in case.get("providers", {}).items():
            for phase in ("samples", "warmup"):
                for sample_index, sample in enumerate(group.get(phase, [])):
                    if (
                        sample.get("provenance", {}).get("kind")
                        != "benchmark-execution-provenance"
                    ):
                        continue
                    observed = source.row(
                        str(case.get("id", case_index)),
                        f"case:{case_index}/{arm}/{phase}:{sample_index}",
                        "execution",
                    )
                    reports.apply_provenance(observed, sample)
                    observed["executionStatus"] = (
                        "succeeded" if sample.get("exitCode") == 0 else "failed"
                    )
                    observed["verdict"] = observed["executionStatus"]
                    observed["limitations"].append(
                        "Package worker observation; qualification remains separately bound."
                    )
                    for name, unit in (
                        ("operationMs", "ms"),
                        ("wallNs", "ns"),
                        ("cpuMs", "ms"),
                    ):
                        if isinstance(sample.get(name), (int, float)):
                            observed["metrics"].append(
                                metric(
                                    name,
                                    unit,
                                    sample[name],
                                    scope=phase,
                                    count=1,
                                )
                            )
                    rows.append(observed)
    for i, result in enumerate(payload.get("rows", [])):
        workload = str(
            result.get("shader")
            or result.get("stage")
            or result.get("variant")
            or "process"
        )
        observed = source.row(
            workload,
            "record:" + str(i),
            "execution"
            if "exitCode" in result or "cohort" in result
            else "aggregate",
        )
        observed["implementation"]["name"] = result.get("arm")
        observed["backend"] = result.get("target")
        if isinstance(payload.get("contract"), dict):
            observed["workloadContractHash"] = reports.stable_hash(
                payload["contract"]
            )
        observed["executedAt"] = reports.execution_times(result)["executedAt"]
        reports.apply_provenance(observed, result)
        observed["executionTimeSource"] = (
            source.registration["path"] + "#rows/" + str(i)
            if observed["executedAt"]
            else None
        )
        if "exitCode" in result:
            observed["executionStatus"] = (
                "succeeded" if result["exitCode"] == 0 else "failed"
            )
            observed["verdict"] = observed["executionStatus"]
        for name, value in result.items():
            if not isinstance(value, (int, float)) or isinstance(value, bool):
                continue
            unit = (
                "ms"
                if name.endswith("Ms")
                else "ns"
                if name.endswith("Ns")
                else "bytes"
                if name.endswith("Bytes")
                else None
            )
            if unit and not name.startswith(("candidate", "baseline")):
                observed["metrics"].append(
                    metric(
                        name,
                        unit,
                        value,
                        scope="recorded process or iteration",
                        count=1,
                    )
                )
        for name in ("runWallNs", "runCpuNs"):
            if isinstance(result.get(name), list):
                for j, value in enumerate(result[name]):
                    observed["metrics"].append(
                        metric(
                            name + "[" + str(j) + "]",
                            "ns",
                            value,
                            scope="iteration within recorded application process",
                            count=1,
                        )
                    )
        for stat in ("P50", "P95"):
            candidate, baseline = (
                result.get("candidate" + stat + "Ns"),
                result.get("baseline" + stat + "Ns"),
            )
            if candidate is not None:
                observed["metrics"].append(
                    metric(
                        stat,
                        "ns",
                        candidate,
                        baseline,
                        candidate / baseline if baseline else None,
                        scope="retained compiler aggregate",
                    )
                )
        if isinstance(result.get("bytesOutEqual"), bool):
            observed["correctness"] = {
                "status": "passed" if result["bytesOutEqual"] else "failed",
                "evidence": source.registration["path"]
                + "#rows/"
                + str(i)
                + "/bytesOutEqual",
            }
        rows.append(observed)
    for i, result in enumerate(payload.get("compilations", [])):
        observed = source.row(
            result["stage"], "compilation:" + str(i), "execution"
        )
        compiler = result["compiler"]
        observed["implementation"].update(
            name=compiler,
            binaryHash=payload.get("inputs", {})
            .get("doe_compiler" if compiler == "doe" else compiler, {})
            .get("sha256"),
        )
        observed["workloadContractHash"] = payload.get("fixtureManifestSha256")
        observed["executionStatus"] = (
            "succeeded" if result["returnCode"] == 0 else "failed"
        )
        observed["verdict"] = observed["executionStatus"]
        observed["metrics"] = [
            metric(
                "processWallMs",
                "ms",
                result["processWallMs"],
                scope="compiler process",
                count=1,
            )
        ]
        rows.append(observed)
    for arm_index, arm in enumerate(payload.get("arms", [])):
        for sequence_index, result in enumerate(arm.get("sequences", [])):
            observed = source.row(
                result["sequence"],
                "arm:" + str(arm_index) + "/sequence:" + str(sequence_index),
                "execution",
            )
            observed["implementation"].update(
                name=arm["runtime"],
                binaryHash=payload.get("inputs", {})
                .get(arm["runtime"] + "_library", {})
                .get("sha256"),
            )
            observed["implementation"]["componentHashes"]["compiler"] = payload[
                "inputs"
            ]["doe_compiler" if arm["compiler"] == "doe" else arm["compiler"]][
                "sha256"
            ]
            observed["workloadContractHash"] = payload.get(
                "fixtureManifestSha256"
            )
            observed["executionStatus"] = (
                "succeeded" if result["returnCode"] == 0 else "failed"
            )
            observed["metrics"] = [
                metric(
                    "processWallMs",
                    "ms",
                    result["processWallMs"],
                    scope="projection process",
                    count=1,
                )
            ]
            for j, sample in enumerate(result.get("observations", [])):
                if sample.get("kind") == "sample":
                    observed["metrics"].append(
                        metric(
                            "iteration:" + str(sample["iteration"]),
                            "ms",
                            sample["ms"],
                            scope="recorded iteration in projection process",
                            count=1,
                        )
                    )
            rows.append(observed)
    for i, result in enumerate(payload.get("comparisons", [])):
        observed = source.row(result["name"], "comparison:" + str(i))
        observed["verdict"] = str(payload.get("performanceClaim", "diagnostic"))
        if "medianRatio" in result:
            observed["metrics"] = [
                metric(
                    result["name"],
                    "ratio",
                    ratio=result["medianRatio"],
                    count=len(result.get("ratios", [])),
                    scope="reported compiler/runtime attribution, not qualification",
                )
            ]
        rows.append(observed)
    return rows
