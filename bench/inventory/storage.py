"""Lossless v3 storage with v2 expansion for inventory consumers.

Shared fields are interned by exact canonical JSON, never by inferred identity.
Metric templates preserve null versus absent fields and every numeric value.
"""

from __future__ import annotations

from copy import deepcopy
import json
from pathlib import Path
from typing import Any

SHARED_FIELDS = (
    "machine",
    "implementation",
    "comparator",
    "evidence",
    "limitations",
    "correctness",
    "fallback",
    "acceptance",
    "qualification",
)
VALUE_FIELDS = ("implementationValue", "comparatorValue", "ratio")


def canonical(value: Any) -> str:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    )


def compact(inventory: dict[str, Any]) -> dict[str, Any]:
    """Encode a logical v2 inventory without changing observation identities."""
    if inventory["schemaVersion"] != 2:
        raise ValueError("compact requires expanded inventory v2")
    tables: dict[str, list[Any]] = {
        key: [] for key in (*SHARED_FIELDS, "metricTemplates")
    }
    seen: dict[str, dict[str, int]] = {key: {} for key in tables}

    def intern(table: str, value: Any) -> int:
        key = canonical(value)
        if key not in seen[table]:
            seen[table][key] = len(tables[table])
            tables[table].append(deepcopy(value))
        return seen[table][key]

    rows = []
    for row in inventory["rows"]:
        stored = {
            k: deepcopy(v)
            for k, v in row.items()
            if k not in (*SHARED_FIELDS, "metrics")
        }
        stored["schemaVersion"] = 3
        for key in SHARED_FIELDS:
            stored[key + "Ref"] = intern(key, row[key])
        metrics = []
        for metric in row["metrics"]:
            keys = [
                key
                for key in VALUE_FIELDS
                if key in metric and metric[key] is not None
            ]
            template = {
                "fields": {k: v for k, v in metric.items() if k not in keys},
                "valueKeys": keys,
            }
            metrics.append(
                [intern("metricTemplates", template), [metric[k] for k in keys]]
            )
        stored["metrics"] = metrics
        rows.append(stored)
    return {
        **{k: deepcopy(v) for k, v in inventory.items() if k != "rows"},
        "schemaVersion": 3,
        "tables": tables,
        "rows": rows,
    }


def expand(inventory: dict[str, Any]) -> dict[str, Any]:
    """Read historical expanded v2 or compact v3 into the same logical shape."""
    version = inventory.get("schemaVersion")
    if version == 2:
        return inventory
    if version != 3:
        raise ValueError(f"Unsupported inventory schemaVersion: {version}")
    tables = inventory["tables"]

    def resolve(table: str, reference: Any) -> Any:
        if type(reference) is not int or not 0 <= reference < len(
            tables[table]
        ):
            raise ValueError(
                f"Invalid inventory reference {table}[{reference}]"
            )
        return deepcopy(tables[table][reference])

    rows = []
    for stored in inventory["rows"]:
        row = {
            k: deepcopy(v)
            for k, v in stored.items()
            if k not in (*(key + "Ref" for key in SHARED_FIELDS), "metrics")
        }
        row["schemaVersion"] = 2
        for key in SHARED_FIELDS:
            row[key] = resolve(key, stored[key + "Ref"])
        metrics = []
        for reference, values in stored["metrics"]:
            template = resolve("metricTemplates", reference)
            keys = template["valueKeys"]
            if len(keys) != len(values) or len(set(keys)) != len(keys):
                raise ValueError("Metric template/value arity mismatch")
            if any(
                key not in VALUE_FIELDS or key in template["fields"]
                for key in keys
            ):
                raise ValueError("Invalid metric value keys")
            metrics.append({**template["fields"], **dict(zip(keys, values))})
        row["metrics"] = metrics
        rows.append(row)
    return {
        **{
            k: deepcopy(v)
            for k, v in inventory.items()
            if k not in ("rows", "tables")
        },
        "schemaVersion": 2,
        "rows": rows,
    }


def load_inventory(path: str | Path) -> dict[str, Any]:
    return expand(json.loads(Path(path).read_text(encoding="utf-8")))
