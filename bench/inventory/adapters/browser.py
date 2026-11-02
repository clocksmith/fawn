"""Read browser evidence without changing recorded scope or measurements."""

from __future__ import annotations

import json
import tarfile
from typing import Any, Iterator

from bench.inventory import model as reports
from bench.inventory.sources import Source, metric, assign_machine


def browser_launches(source: Source) -> Iterator[tuple[str, dict[str, Any]]]:
    ref = source.reference("launches")
    if ref and ref["availability"] == "retained":
        with tarfile.open(source.root / ref["path"], "r:gz") as archive:
            for member in archive:
                if (
                    "/launches/" in member.name
                    and member.name.endswith(".json")
                    and ".spec." not in member.name
                ):
                    stream = archive.extractfile(member)
                    if stream is not None:
                        yield member.name, json.load(stream)


def browser_fresh_launch(source: Source) -> list[dict[str, Any]]:
    from bench.browser.fresh_launch.analyze import METRICS, launch_metrics

    units = {name: unit for name, _label, unit, _stat, _lower in METRICS}
    launches = list(browser_launches(source))
    rows = []
    for page, section in source.payload.get("pages", {}).items():
        for comparison, values in section["comparisons"].items():
            candidate, baseline = comparison.split("/")
            row = source.row(page, comparison)
            row["implementation"]["name"] = candidate
            row["comparator"]["name"] = baseline
            selected = [
                r
                for _, r in launches
                if r["spec"]["page"] == page
                and r["spec"]["arm"] in (candidate, baseline)
            ]
            ends = [
                r.get("finishedAt")
                for r in selected
                if reports.parse_utc_iso(r.get("finishedAt"))
            ]
            if ends:
                row["executedAt"] = reports.iso_utc(
                    max(reports.parse_utc_iso(t) for t in ends)
                )
                row["executionTimeSource"] = (
                    source.reference("launches")["path"]
                    + "#max(finishedAt) for page/arms"
                )
            if selected:
                assign_machine(
                    row,
                    selected[0].get("host", {}),
                    "launch.host",
                    selected[0].get("adapterInfo"),
                )
                row["backend"] = (
                    "d3d12"
                    if row["machine"]["os"] == "win32"
                    else "metal"
                    if row["machine"]["os"] == "darwin"
                    else None
                )
            row["executionStatus"] = (
                "partially-failed"
                if any(r.get("ok") is False for r in selected)
                else "succeeded"
                if selected
                else "unknown"
            )
            for name, value in values.items():
                arm = (
                    section["arms"]
                    .get(candidate, {})
                    .get("metrics", {})
                    .get(name, {})
                )
                outcome = {"no clear difference": "inconclusive"}.get(
                    value["verdict"], value["verdict"]
                )
                row["metrics"].append(
                    metric(
                        name,
                        value.get("unit"),
                        value.get("candidate"),
                        value.get("baseline"),
                        value.get("ratio"),
                        scope=value.get("label", name),
                        ci=value.get("ci95"),
                        count=arm.get("n"),
                        outcome=outcome,
                    )
                )
            envelopes = [r.get("provenance", {}) for r in selected]
            if envelopes and all(
                e.get("kind") == "benchmark-execution-provenance"
                for e in envelopes
            ):
                for launch in selected:
                    checked = source.row(page)
                    reports.apply_provenance(checked, launch)
                hosts = {e["machine"]["id"] for e in envelopes}
                if len(hosts) == 1 and None not in hosts:
                    row["machine"].update(envelopes[0]["machine"])
                    for side, arm in (
                        ("implementation", candidate),
                        ("comparator", baseline),
                    ):
                        identities = [
                            r["provenance"]["implementation"]
                            for r in selected
                            if r["spec"]["arm"] == arm
                        ]
                        if identities and all(
                            i == identities[0] for i in identities
                        ):
                            row[side].update(
                                {
                                    k: v
                                    for k, v in identities[0].items()
                                    if v is not None
                                }
                            )
                else:
                    row["limitations"].append(
                        "Launch receipts bind different or missing machines; comparison host remains unknown."
                    )
            row["verdict"] = "diagnostic"
            row["limitations"].append(
                "Browser integration evidence cannot qualify native runtime or workspace behavior."
            )
            rows.append(row)
    for member, payload in launches:
        spec = payload["spec"]
        row = source.row(spec["page"], member, "execution")
        row.update(reports.execution_times(payload))
        row["executionTimeSource"] = (
            source.reference("launches")["path"] + "#" + member + "/finishedAt"
            if row["executedAt"]
            else None
        )
        assign_machine(
            row,
            payload.get("host", {}),
            "launch.host",
            payload.get("adapterInfo"),
        )
        row["backend"] = (
            "d3d12"
            if row["machine"]["os"] == "win32"
            else "metal"
            if row["machine"]["os"] == "darwin"
            else None
        )
        row["implementation"]["name"] = spec["arm"]
        reports.apply_provenance(row, payload)
        row["executionStatus"] = "succeeded" if payload.get("ok") else "failed"
        row["verdict"] = "observed" if payload.get("ok") else "failed"
        oracle = payload.get("oracle", {})
        if oracle and all(
            oracle.get(k, {}).get("matched") for k in ("first", "final")
        ):
            row["correctness"] = {
                "status": "passed",
                "evidence": member + "#oracle",
            }
        else:
            row["limitations"].append(
                "No full independent output oracle retained for this launch; visual checks do not substitute."
            )
        if (
            spec.get("expectDoe")
            and payload.get("provider", {}).get("doeRuntimeMapped") is True
        ):
            row["fallback"] = {
                "status": "provider-observed",
                "evidence": member + "#provider",
            }
        row["metrics"] = [
            metric(
                k,
                units.get(k),
                value,
                scope="single fresh browser launch",
                count=1,
            )
            for k, value in launch_metrics(payload).items()
        ]
        rows.append(row)
    return rows
