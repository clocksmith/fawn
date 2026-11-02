"""Observed provenance for new measurements; never repair historical receipts.

Call after measured work, using its actual completion clock. File hashing and
host queries are outside timing. GPU/driver values require runtime observations;
checkout HEAD cannot identify an independently built or preexisting executable.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import platform
import sys
from typing import Any

from bench.inventory.model import iso_utc, parse_utc_iso


def artifact_stat(path: Path) -> dict[str, str]:
    """Metadata only: do not read/warm executable pages before cold measurements."""
    value = path.stat()
    return {
        "dev": str(value.st_dev),
        "ino": str(value.st_ino),
        "size": str(value.st_size),
        "mtimeNs": str(value.st_mtime_ns),
        "ctimeNs": str(value.st_ctime_ns),
    }


def file_hash(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def artifact_identity(
    spec: dict[str, Any] | None,
) -> tuple[dict[str, Any], list[str]]:
    spec = spec or {}
    result = {
        "name": spec.get("name"),
        "sourceCommit": None,
        "binaryHash": None,
        "componentHashes": {},
    }
    limitations = []
    binary = Path(spec["binaryPath"]) if spec.get("binaryPath") else None
    if binary and binary.is_file():
        before_hash_stat = artifact_stat(binary)
        observed_hash = file_hash(binary)
        after_hash_stat = artifact_stat(binary)
        if before_hash_stat != after_hash_stat or (
            spec.get("expectedStat") is not None
            and spec["expectedStat"] != after_hash_stat
        ):
            limitations.append(
                "Binary changed during measurement; binaryHash is unknown."
            )
        elif (
            spec.get("expectedBinaryHash")
            and spec["expectedBinaryHash"] != observed_hash
        ):
            limitations.append(
                "Binary changed during measurement; binaryHash is unknown."
            )
        else:
            result["binaryHash"] = observed_hash
    else:
        limitations.append(
            "Measured binary path unavailable; binaryHash is unknown."
        )
    for name, value in sorted(spec.get("componentPaths", {}).items()):
        path = Path(value)
        if path.is_file():
            result["componentHashes"][name] = file_hash(path)
        else:
            limitations.append(
                f"Component {name} unavailable; no hash recorded."
            )
    receipt = (
        Path(spec["buildReceiptPath"]) if spec.get("buildReceiptPath") else None
    )
    if receipt and receipt.is_file() and result["binaryHash"]:
        binding = json.loads(receipt.read_text(encoding="utf-8"))
        commit = binding.get("sourceCommit")
        hashes = [binding.get("binaryHash"), binding.get("native_dll_sha256")]
        hashes += [
            v.get("sha256")
            for v in binding.get("artifacts", {}).values()
            if isinstance(v, dict)
        ]
        if (
            isinstance(commit, str)
            and len(commit) == 40
            and all(c in "0123456789abcdef" for c in commit)
            and result["binaryHash"] in hashes
            and not binding.get("sourceStatus")
        ):
            result["sourceCommit"] = commit
            result["componentHashes"]["buildReceipt"] = file_hash(receipt)
    if result["sourceCommit"] is None:
        limitations.append(
            "No matching source-bound build receipt; sourceCommit is unknown."
        )
    return result, limitations


def capture_provenance(
    *,
    executed_at: str,
    implementation: dict[str, Any],
    comparator: dict[str, Any] | None = None,
    machine_observation: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Bind one execution clock to observed host and actual artifact bytes."""
    parsed = parse_utc_iso(executed_at)
    if parsed is None:
        raise ValueError(
            "Provenance requires an execution timestamp with timezone"
        )
    host = platform.node()
    machine = {
        "id": host or None,
        "label": host or None,
        "identitySource": "platform.node()" if host else None,
        "os": sys.platform,
        "osVersion": platform.platform(),
        "architecture": platform.machine(),
        "gpu": None,
        "driver": None,
    }
    observed = machine_observation or {}
    # Only actual runtime adapter observations fill GPU/driver, never a model guess.
    for key in ("gpu", "driver"):
        if observed.get(key) is not None:
            machine[key] = observed[key]
    if observed.get("id") and observed.get("identitySource"):
        machine["id"], machine["identitySource"] = (
            observed["id"],
            observed["identitySource"],
        )
    impl, impl_limits = artifact_identity(implementation)
    comp, comp_limits = artifact_identity(comparator)
    limits = ["implementation: " + reason for reason in impl_limits]
    limits += ["comparator: " + reason for reason in comp_limits]
    limits += [
        "machine." + key + " is unknown; no runtime observation."
        for key in ("id", "gpu", "driver")
        if machine[key] is None
    ]
    return {
        "schemaVersion": 1,
        "kind": "benchmark-execution-provenance",
        "executedAt": iso_utc(parsed),
        "executionTimeSource": "receipt#/provenance/executedAt",
        "machine": machine,
        "implementation": impl,
        "comparator": comp,
        "limitations": limits,
    }


def validate_provenance(value: dict[str, Any]) -> None:
    import jsonschema

    schema = (
        Path(__file__).resolve().parents[2]
        / "config/benchmark-receipt-provenance.schema.json"
    )
    jsonschema.Draft202012Validator(
        json.loads(schema.read_text(encoding="utf-8")),
        format_checker=jsonschema.FormatChecker(),
    ).validate(value)


def main() -> int:
    """Browser bridge: JSON request on stdin; the same capture helper on stdout."""
    request = json.load(sys.stdin)
    value = capture_provenance(
        executed_at=request["executedAt"],
        implementation=request["implementation"],
        comparator=request.get("comparator"),
        machine_observation=request.get("machine"),
    )
    print(json.dumps(value, sort_keys=True, allow_nan=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
