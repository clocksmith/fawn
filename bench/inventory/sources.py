"""Receipt adapters for the existing benchmark cube inventory mode.

Adapters expose recorded observations, never recover missing samples, infer
execution from report dates, or promote a diagnostic result into qualification.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
from typing import Any, Iterator

from bench.inventory import model as reports


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_payload(path: Path) -> Any:
    if path.name.endswith(".gz"):
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            return json.load(stream)
    return json.loads(path.read_text(encoding="utf-8"))


@dataclass
class Source:
    root: Path
    registration: dict[str, Any]
    references: list[dict[str, Any]]
    payload: dict[str, Any]
    policy: dict[str, Any]
    cache: dict[str, Any] = field(default_factory=dict)

    def reference(self, role: str) -> dict[str, Any] | None:
        return next((r for r in self.references if r["role"] == role), None)

    def data(self, role: str) -> Any:
        if role in self.cache:
            return self.cache[role]
        ref = self.reference(role)
        if ref is None or ref["availability"] != "retained":
            return None
        path = self.root / ref["path"]
        if path.suffix in (".txt", ".md"):
            self.cache[role] = path.read_text(encoding="utf-8")
        else:
            self.cache[role] = read_payload(path)
        return self.cache[role]

    def row(
        self,
        workload: str,
        execution: str = "summary",
        level: str = "aggregate",
    ) -> dict[str, Any]:
        row = reports.inventory_row(
            self.registration,
            workload,
            self.registration["id"] + "/" + workload + "/" + execution,
            level=level,
        )
        row["evidence"] = self.references.copy()
        row.update(reports.execution_times(self.payload))
        if level == "aggregate" and execution == "summary":
            reports.apply_provenance(row, self.payload)
        row["limitations"].extend(self.registration.get("limitations", []))
        for ref in self.references:
            if ref["availability"] != "retained":
                row["limitations"].append(
                    ref["role"]
                    + " evidence "
                    + ref["availability"]
                    + ": "
                    + str(ref["path"])
                )
        return row


def evidence_reference(root: Path, reference: dict[str, Any]) -> dict[str, Any]:
    """Verify exact bytes and prohibit ephemeral output as durable evidence."""
    result = {**reference, "availability": "missing"}
    if reference["path"] is None:
        return result
    path = root / reference["path"]
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("Evidence outside repository: " + str(path))
    if not path.is_file():
        return result
    actual = file_hash(path)
    if actual != reference["sha256"]:
        raise ValueError("Evidence checksum mismatch: " + reference["path"])
    ignored = (
        subprocess.run(
            [
                "git",
                "-C",
                str(root),
                "check-ignore",
                "--quiet",
                reference["path"],
            ],
            capture_output=True,
        ).returncode
        == 0
    )
    tracked = subprocess.run(
        [
            "git",
            "-C",
            str(root),
            "ls-files",
            "--error-unmatch",
            "--",
            reference["path"],
        ],
        capture_output=True,
    ).returncode
    # Return 128 permits standalone fixture roots outside Git. In the actual
    # repository, an untracked file is not durable until retained in the index.
    result["availability"] = (
        "ephemeral" if ignored or tracked == 1 else "retained"
    )
    return result


def make_source(
    root: Path, registration: dict[str, Any], policy: dict[str, Any]
) -> Source:
    refs = [
        evidence_reference(
            root,
            {
                "role": "report",
                "path": registration["path"],
                "sha256": registration["sha256"],
                "required": True,
            },
        )
    ]
    refs += [
        evidence_reference(root, r) for r in registration.get("provenance", [])
    ]
    payload = (
        read_payload(root / registration["path"])
        if refs[0]["availability"] == "retained"
        else {}
    )
    return Source(root, registration, refs, payload, policy)


def metric(
    name: str,
    unit: str | None,
    value: Any = None,
    comparator: Any = None,
    ratio: Any = None,
    *,
    scope: str = "reported aggregate",
    count: int | None = None,
    ci: list[float] | None = None,
    outcome: str = "unknown",
    policy: dict[str, Any] | None = None,
) -> dict[str, Any]:
    record = {
        "name": name,
        "unit": unit,
        "scope": scope,
        "direction": "lower"
        if unit in ("ms", "ns", "s", "MB", "MiB", "bytes")
        else "unspecified",
        "implementationValue": value,
        "comparatorValue": comparator,
        "ratio": ratio,
        "samples": {
            "implementation": count,
            "comparator": count
            if comparator is not None or ratio is not None
            else None,
            "unit": "observations" if count is not None else None,
        },
        "uncertainty": {
            "kind": "reported confidence interval",
            "level": 0.95,
            "bounds": ci,
            "unit": "ratio",
        }
        if ci is not None
        else None,
        "outcome": outcome,
        "policy": policy,
    }
    return {
        key: value
        for key, value in record.items()
        if value is not None or key == "unit"
    }


def phase_unit(name: str) -> str | None:
    if name in ("maxRssMiB", "max_rss_mib"):
        return "MiB"
    if name in (
        "frameworkImagesBeforeSetup",
        "imagesBeforeSetup",
        "providerImagesLoaded",
        "setupImagesLoaded",
    ):
        return "count"
    # process_metrics converts *Ns to milliseconds; preserve the original name.
    return "ms"


def assign_machine(
    row: dict[str, Any],
    host: dict[str, Any],
    source: str,
    adapter: dict[str, Any] | None = None,
) -> None:
    adapter = adapter or {}
    machine = row["machine"]
    hostname = host.get("hostname") or host.get("machineId")
    machine.update(
        {
            "id": hostname,
            "label": hostname or host.get("label"),
            "os": host.get("platform") or host.get("os"),
            "osVersion": host.get("release") or host.get("kernel"),
            "architecture": host.get("arch"),
            "gpu": adapter.get("device")
            or adapter.get("description")
            or host.get("gpu"),
            "driver": host.get("driver") or adapter.get("driver"),
            "identitySource": source if hostname else None,
        }
    )
    if isinstance(machine["os"], dict):
        machine["os"] = None


def native_catalog(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.native import native_catalog as adapt

    return adapt(source)


def bind_phase(
    row: dict[str, Any], source: Source, provider: str, control: str
) -> None:
    from bench.inventory.adapters.phases import bind_phase as adapt

    adapt(row, source, provider, control)


def phase_cohort(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.phases import phase_cohort as adapt

    return adapt(source)


def browser_launches(source: Source) -> Iterator[tuple[str, dict[str, Any]]]:
    from bench.inventory.adapters.browser import browser_launches as adapt

    return adapt(source)


def browser_fresh_launch(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.browser import browser_fresh_launch as adapt

    return adapt(source)


def workspace(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.workspace import workspace as adapt

    return adapt(source)


def compare_report(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.comparisons import compare_report as adapt

    return adapt(source)


def application(source: Source) -> list[dict[str, Any]]:
    from bench.inventory.adapters.comparisons import application as adapt

    return adapt(source)


ADAPTERS = {
    "native-catalog": native_catalog,
    "phase-cohort": phase_cohort,
    "browser-fresh-launch": browser_fresh_launch,
    "workspace": workspace,
    "compare-report": compare_report,
    "application": application,
}


def normalize_source(
    root: Path, registration: dict[str, Any], policy: dict[str, Any]
) -> list[dict[str, Any]]:
    expected = policy["adapters"].get(registration["adapter"])
    if (
        expected != registration["adapterVersion"]
        or registration["adapter"] not in ADAPTERS
    ):
        raise ValueError(
            "Unsupported inventory adapter version: " + registration["adapter"]
        )
    if (
        registration["section"]
        not in policy["adapterSections"][registration["adapter"]]
    ):
        raise ValueError(
            "Adapter cannot change evidence surface: " + registration["adapter"]
        )
    source = make_source(root, registration, policy)
    if not source.payload:
        row = source.row(registration["familyId"], "unavailable", "unavailable")
        row["executionStatus"] = (
            "not-run" if registration["path"] is None else "unknown"
        )
        row["limitations"].append(
            "Registered measurement evidence unavailable; no result is invented."
        )
        return [reports.finalize_inventory_row(row, policy["grouping"])]
    rows = ADAPTERS[registration["adapter"]](source)
    if not rows:
        row = source.row(registration["familyId"])
        row["limitations"].append(
            "Source format contains no recognized individual observations."
        )
        rows = [row]
    return [
        reports.finalize_inventory_row(row, policy["grouping"]) for row in rows
    ]
