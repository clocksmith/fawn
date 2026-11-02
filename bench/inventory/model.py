"""Receipt clocks, observation identity and current-result selection."""

from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
from typing import Any


def parse_utc_iso(value: Any) -> datetime | None:
    if not isinstance(value, str) or not value.strip():
        return None
    candidate = value.strip()
    if candidate.endswith("Z"):
        candidate = candidate[:-1] + "+00:00"
    try:
        parsed = datetime.fromisoformat(candidate)
    except ValueError:
        return None
    if parsed.tzinfo is None:
        return None
    return parsed.astimezone(timezone.utc)


def iso_utc(value: datetime | None) -> str | None:
    if value is None:
        return None
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def stable_hash(value: Any) -> str:
    return hashlib.sha256(
        json.dumps(
            value, sort_keys=True, separators=(",", ":"), allow_nan=False
        ).encode()
    ).hexdigest()


def execution_times(payload: dict[str, Any]) -> dict[str, Any]:
    """Keep report, contract and execution clocks independent."""

    def stamp(keys: tuple[str, ...]) -> tuple[str | None, str | None]:
        for key in keys:
            parsed = parse_utc_iso(payload.get(key))
            if parsed is not None:
                return iso_utc(parsed), key
        return None, None

    executed, source = stamp(
        ("executedAt", "finishedAt", "endedAt", "startedAt")
    )
    provenance = payload.get("provenance", {})
    if (
        executed is None
        and isinstance(provenance, dict)
        and provenance.get("kind") == "benchmark-execution-provenance"
    ):
        observed = parse_utc_iso(provenance.get("executedAt"))
        if observed:
            executed = iso_utc(observed)
            source = "provenance.executedAt"
    reported, _ = stamp(("reportedAt", "generatedAt", "generatedAtUtc"))
    frozen, _ = stamp(("contractFrozenAt", "frozenAtUtc"))
    return {
        "executedAt": executed,
        "executionTimeSource": source,
        "reportedAt": reported,
        "contractFrozenAt": frozen,
    }


def apply_provenance(row: dict[str, Any], payload: dict[str, Any]) -> None:
    """Apply only an explicit new-receipt envelope; historical rows are untouched."""
    provenance = payload.get("provenance")
    if (
        not isinstance(provenance, dict)
        or provenance.get("kind") != "benchmark-execution-provenance"
    ):
        return
    from bench.lib import receipt_provenance

    receipt_provenance.validate_provenance(provenance)
    for field in ("machine", "implementation", "comparator"):
        for key, value in provenance[field].items():
            if key == "componentHashes":
                row[field][key].update(value)
            elif value is not None:
                row[field][key] = value
    row["executedAt"] = provenance["executedAt"]
    row["executionTimeSource"] = provenance["executionTimeSource"]
    row["limitations"].extend(provenance["limitations"])


def inventory_row(
    registration: dict[str, Any],
    workload: str,
    execution: str,
    *,
    level: str = "aggregate",
) -> dict[str, Any]:
    """Make an unknown-safe row; adapters only fill receipt-backed facts."""
    identity = [registration["id"], workload, execution, level]
    return {
        "schemaVersion": 2,
        "rowId": stable_hash(identity),
        "registrationId": registration["id"],
        "familyId": registration["familyId"],
        "section": registration["section"],
        "comparisonType": registration["comparisonType"],
        "workloadId": workload,
        "workloadContractHash": None,
        "campaignId": registration["campaignId"],
        "executionId": execution,
        "observationLevel": level,
        "machine": {
            "id": None,
            "label": None,
            "os": None,
            "osVersion": None,
            "architecture": None,
            "gpu": None,
            "driver": None,
            "identitySource": None,
        },
        "backend": None,
        "implementation": {
            "name": None,
            "sourceCommit": None,
            "binaryHash": None,
            "componentHashes": {},
        },
        "comparator": {
            "name": None,
            "sourceCommit": None,
            "binaryHash": None,
            "componentHashes": {},
        },
        **execution_times({}),
        "executionTimeSource": None,
        "executionStatus": "unknown",
        "metrics": [],
        "correctness": {"status": "unknown", "evidence": None},
        "fallback": {"status": "unknown", "evidence": None},
        "acceptance": {"status": "unknown", "evidence": None},
        "qualification": {
            "status": "unqualified",
            "scope": None,
            "reason": "No independently bound qualification receipt.",
        },
        "verdict": "unknown",
        "evidence": [],
        "limitations": [],
        "supersedes": registration.get("supersedes", []),
    }


def finalize_inventory_row(
    row: dict[str, Any], grouping: dict[str, Any] | None = None
) -> dict[str, Any]:
    """Give unknown machines isolated identities and expose absent evidence."""
    machine = row["machine"]
    if machine["id"] is None:
        machine["id"] = (
            "unknown:"
            + stable_hash([row["registrationId"], row["executionId"]])[:24]
        )
        machine["identitySource"] = None
        row["limitations"].append(
            "Actual machine identity is not bound by the retained receipt."
        )
    for key in ("os", "osVersion", "architecture", "gpu", "driver"):
        if machine[key] is None:
            row["limitations"].append("machine." + key + " is unknown.")
    for key in (
        "executedAt",
        "reportedAt",
        "contractFrozenAt",
        "workloadContractHash",
    ):
        if row[key] is None:
            row["limitations"].append(
                key + " is unknown; no receipt value was retained."
            )
    for side in ("implementation", "comparator"):
        for key in ("sourceCommit", "binaryHash"):
            if row[side][key] is None:
                row["limitations"].append(side + "." + key + " is unknown.")
    row["limitations"] = sorted(set(row["limitations"]))

    # The series compares observations across measured builds, while artifactKey
    # keeps their exact source/binary pairs separate. Unknown contracts never join.
    def field(path: str) -> Any:
        value: Any = row
        for part in path.split("."):
            value = value[part]
        if path == "workloadContractHash" and value is None:
            return "unknown:" + row["registrationId"]
        return value

    grouping = grouping or {
        "seriesFields": [
            "section",
            "comparisonType",
            "workloadId",
            "workloadContractHash",
            "machine.id",
            "machine.os",
            "backend",
            "observationLevel",
            "implementation.name",
            "comparator.name",
        ],
        "artifactFields": ["implementation", "comparator"],
    }
    row["seriesKey"] = stable_hash(
        [field(path) for path in grouping["seriesFields"]]
    )
    row["artifactKey"] = stable_hash(
        [field(path) for path in grouping["artifactFields"]]
    )
    return row


def inventory_qualified(row: dict[str, Any]) -> bool:
    """Qualification is explicitly scoped and cannot cross evidence surfaces."""
    return (
        row["comparisonType"]
        in {
            "native-runtime": ("native-provider",),
            "browser-integration": ("browser-provider",),
            "compiler": ("compiler-provider",),
            "application": ("application-provider", "package-provider"),
            "workspace": ("workspace-behavior",),
        }[row["section"]]
        and row["qualification"]["status"] == "qualified"
        and row["qualification"]["scope"] == row["section"]
        and row["acceptance"]["status"] == "passed"
        and row["correctness"]["status"] == "passed"
        and row["fallback"]["status"] == "disabled"
        and row["executionStatus"] == "succeeded"
        and not row["machine"]["id"].startswith("unknown:")
        and all(
            row[side]["sourceCommit"] and row[side]["binaryHash"]
            for side in ("implementation", "comparator")
        )
        and bool(row["workloadContractHash"])
        and bool(row["executionTimeSource"])
        and bool(row["evidence"])
        and all(
            e["availability"] == "retained"
            for e in row["evidence"]
            if e["required"]
        )
    )


def select_inventory_current(
    rows: list[dict[str, Any]], current_source: str
) -> list[dict[str, Any]]:
    """Select by execution only, retain undated observations and older qualifications."""
    ids = {row["rowId"] for row in rows}
    registrations = {row["registrationId"] for row in rows}
    for row in rows:
        for target in row["supersedes"]:
            if target not in ids and target not in registrations:
                raise ValueError("Unknown supersedes reference: " + target)
            if target in (row["rowId"], row["registrationId"]):
                raise ValueError("Self supersession: " + target)
    edges = {r["rowId"]: set(r["supersedes"]) for r in rows}
    for row in rows:
        edges.setdefault(row["registrationId"], set()).update(row["supersedes"])

    def visit(key: str, active: set[str]) -> None:
        if key in active:
            raise ValueError("Cyclic supersession: " + key)
        for target in edges.get(key, ()):
            visit(target, active | {key})

    for key in edges:
        visit(key, set())
    groups: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        groups.setdefault(row["seriesKey"], []).append(row)
    selections = []
    for key, group in sorted(groups.items()):
        dated = [r for r in group if parse_utc_iso(r["executedAt"]) is not None]
        qualified = [r for r in dated if inventory_qualified(r)]

        def latest(candidates: list[dict[str, Any]]) -> str | None:
            if not candidates:
                return None
            stamp = max(parse_utc_iso(r["executedAt"]) for r in candidates)
            tied = [
                r for r in candidates if parse_utc_iso(r["executedAt"]) == stamp
            ]
            # Explicit supersession breaks equal-time ties only, never erases history.
            replaced = {s for r in tied for s in r["supersedes"]}
            tied = [
                r
                for r in tied
                if r["rowId"] not in replaced
                and r["registrationId"] not in replaced
            ] or tied
            return min(r["rowId"] for r in tied)

        selections.append(
            {
                "seriesKey": key,
                "latestMeasured": latest(dated),
                "latestQualified": latest(qualified),
                "undated": sorted(
                    r["rowId"] for r in group if r["executedAt"] is None
                ),
                "artifacts": sorted({r["artifactKey"] for r in group}),
                "observations": sorted(r["rowId"] for r in group),
                "currentSourceMeasured": any(
                    r["implementation"]["sourceCommit"] == current_source
                    and r["implementation"]["binaryHash"]
                    and r["executedAt"]
                    and r["executionStatus"] != "not-run"
                    for r in group
                ),
            }
        )
    return selections
