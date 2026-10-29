"""Validate same-execution plan readbacks against their pinned command input."""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
import struct
from typing import Any


_REPO_ROOT = Path(__file__).resolve().parents[2]
_SHA256 = re.compile(r"[0-9a-f]{64}\Z")
_MAX_PLAN_BYTES = 32 * 1024 * 1024


def _field(value: dict[str, Any], *names: str, default: Any = None) -> Any:
    return next((value[name] for name in names if name in value), default)


def _integer(value: Any, *, minimum: int = 0) -> int:
    if type(value) is not int or value < minimum:
        raise ValueError(f"expected integer >= {minimum}, got {value!r}")
    return value


def _digest(value: Any) -> str:
    if not isinstance(value, str) or not _SHA256.fullmatch(value):
        raise ValueError("missing or invalid SHA256")
    return value


def _float32(value: Any) -> float:
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError("invalid frozen tolerance")
    return struct.unpack("<f", struct.pack("<f", value))[0]


def _source_path(value: Any) -> Path:
    if not isinstance(value, str) or not value:
        raise ValueError("missing pinned file path")
    path = Path(value)
    return path if path.is_absolute() else _REPO_ROOT / path


def _validate_sample(sample: dict[str, Any]) -> tuple[Any, ...]:
    meta = sample.get("traceMeta")
    if not isinstance(meta, dict):
        raise ValueError("missing trace metadata")
    proof = meta.get("timedOutputProof")
    if not isinstance(proof, dict) or proof.get("schema") != "doe.plan-output-proof/v1":
        raise ValueError("missing same-execution plan output proof")
    if proof.get("executionScope") != "same-execution":
        raise ValueError("output belongs to isolated or unknown execution")
    path = _source_path(meta.get("hostPlanArtifactPath"))
    if path.stat().st_size > _MAX_PLAN_BYTES:
        raise ValueError("pinned plan exceeds bounded plan reader")
    raw = path.read_bytes()
    file_hash = hashlib.sha256(raw).hexdigest()
    if _digest(proof.get("planFileSha256")) != file_hash:
        raise ValueError("plan bytes changed after execution")
    plan = json.loads(raw)
    if not isinstance(plan, dict):
        raise ValueError("invalid plan object")
    plan_hash = _digest(plan.get("planSha256"))
    if proof.get("planSha256") != plan_hash or meta.get("hostPlanArtifactHash") != plan_hash:
        raise ValueError("plan identity mismatch")
    commands = plan.get("commands")
    if not isinstance(commands, list):
        raise ValueError("missing plan commands")
    declared = [(seq, cmd, _field(cmd, "output_oracle", "outputOracle"))
                for seq, cmd in enumerate(commands)
                if isinstance(cmd, dict) and _field(cmd, "output_oracle", "outputOracle") is not None]
    observations = proof.get("observations")
    if not declared or not isinstance(observations, list) or len(observations) != len(declared):
        raise ValueError("missing, duplicate, or extra command output observations")
    count = len(declared)
    for key, expected in (("outputOracleCount", count), ("outputOracleMatchedCount", count),
                          ("outputOracleFailedCount", 0), ("executionErrorCount", 0),
                          ("executionUnsupportedCount", 0), ("executionSkippedCount", 0)):
        if _integer(meta.get(key)) != expected:
            raise ValueError(f"{key} does not match observed successful commands")
    if sample.get("returnCode", 0) != 0 or sample.get("success", True) is not True:
        raise ValueError("failed measured execution")
    identities: list[tuple[Any, ...]] = []
    for (seq, command, oracle), observed in zip(declared, observations, strict=True):
        if not isinstance(oracle, dict) or not isinstance(observed, dict):
            raise ValueError("invalid oracle or observation")
        schema = _field(oracle, "schema_version", "schemaVersion")
        if schema not in (2, 3) or oracle.get("scope") != "command_graph":
            raise ValueError("isolated or unsupported oracle declaration")
        if _field(oracle, "reference_class", "referenceClass") != "independent_v1":
            raise ValueError("independent reference required")
        if oracle.get("initialization") != "zero_fill_v1":
            raise ValueError("unsupported oracle initialization")
        if _integer(_field(oracle, "dispatch_count", "dispatchCount"), minimum=1) != 1:
            raise ValueError("oracle dispatch count differs from the plan execution")
        if _integer(command.get("repeat", 1), minimum=1) != 1:
            raise ValueError("unrepresented repeated plan dispatch")
        group = _integer(_field(oracle, "binding_group", "bindingGroup"))
        binding_id = _integer(oracle.get("binding"))
        bindings = command.get("bindings")
        if not isinstance(bindings, list):
            raise ValueError("missing command bindings")
        targets = [b for b in bindings if isinstance(b, dict)
                   and _field(b, "group", "groupIndex", "group_index", default=0) == group
                   and b.get("binding") == binding_id]
        if len(targets) != 1:
            raise ValueError("oracle target does not resolve to exactly one binding")
        target = targets[0]
        handle = _integer(_field(target, "resource_handle", "resourceHandle", "handle"))
        length = _integer(_field(target, "buffer_size", "bufferSize"), minimum=1)
        identity = {"commandSeq": seq, "kernel": command.get("kernel"),
                    "entryPoint": _field(command, "entry_point", "entryPoint", default="main"),
                    "bindingGroup": group, "binding": binding_id, "resourceHandle": handle,
                    "byteLength": length, "dispatchCount": 1}
        if command.get("kind") != "kernel_dispatch" or not identity["kernel"]:
            raise ValueError("output observation does not name a dispatch")
        for key, value in identity.items():
            observed_value = (_integer(observed.get(key)) if type(value) is int
                              else observed.get(key))
            if observed_value != value:
                raise ValueError("output command sequence, shader, or binding mismatch")
        frozen = observed.get("oracle")
        if not isinstance(frozen, dict):
            raise ValueError("missing frozen oracle binding")
        expected = _digest(_field(oracle, "reference_sha256", "referenceSha256") if schema == 3
                           else _field(oracle, "expected_sha256", "expectedSha256"))
        kind = "float32_reference_tolerance_v1" if schema == 3 else "sha256_exact_v1"
        abs_tol = _float32(_field(oracle, "absolute_tolerance", "absoluteTolerance", default=0))
        rel_tol = _float32(_field(oracle, "relative_tolerance", "relativeTolerance", default=0))
        frozen_identity = {"kind": kind, "referenceClass": "independent_v1", "expectedSha256": expected,
                           "referenceId": _field(oracle, "reference_id", "referenceId"),
                           "referencePath": _field(oracle, "reference_path", "referencePath"),
                           "absoluteTolerance": abs_tol, "relativeTolerance": rel_tol}
        normalized_frozen = {**frozen,
                             "absoluteTolerance": _float32(frozen.get("absoluteTolerance")),
                             "relativeTolerance": _float32(frozen.get("relativeTolerance"))}
        if oracle.get("kind") != kind or any(normalized_frozen.get(k) != v for k, v in frozen_identity.items()):
            raise ValueError("frozen oracle identity or tolerance changed")
        if not isinstance(frozen_identity["referenceId"], str) or not frozen_identity["referenceId"]:
            raise ValueError("missing independent reference identity")
        actual = _digest(observed.get("actualSha256"))
        if observed.get("matched") is not True or _integer(observed.get("mismatchCount")) != 0:
            raise ValueError("observed output failed its independent oracle")
        if schema == 2:
            if actual != expected:
                raise ValueError("exact output hash differs from frozen reference")
        else:
            if length % 4 or _integer(observed.get("comparedValueCount")) != length // 4:
                raise ValueError("not every output float was compared")
            if abs_tol == 0 and rel_tol == 0:
                raise ValueError("missing float comparison tolerance")
            reference_path = _source_path(frozen_identity["referencePath"])
            if reference_path.stat().st_size != length:
                raise ValueError("independent reference byte length changed")
            reference = reference_path.read_bytes()
            if len(reference) != length or hashlib.sha256(reference).hexdigest() != expected:
                raise ValueError("independent reference bytes changed")
        identities.append((*identity.values(), json.dumps(frozen_identity, sort_keys=True), actual))
    return file_hash, plan_hash, tuple(identities)


def assess_plan_output_proof(
    baseline_samples: list[dict[str, Any]], comparison_samples: list[dict[str, Any]],
) -> tuple[bool, dict[str, Any]]:
    """Admit only complete, pinned, equal same-execution output observations.

    Count-only and isolated validation receipts remain rejected. This gate also
    requires identical actual digests across arms, even for tolerance oracles.
    """
    errors: list[str] = []
    signatures: list[set[tuple[Any, ...]]] = []
    for role, samples in (("baseline", baseline_samples), ("comparison", comparison_samples)):
        values: set[tuple[Any, ...]] = set()
        if not samples:
            errors.append(f"{role}: no measured samples")
        for index, sample in enumerate(samples):
            try:
                values.add(_validate_sample(sample))
            except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
                errors.append(f"{role} sample {index}: {error}")
        signatures.append(values)
    if len(signatures[0]) != 1 or signatures[0] != signatures[1]:
        errors.append("both measured sides must have one identical pinned plan/output signature")
    return not errors, {"sameExecutionPlanOutputErrors": errors,
                        "baselineSampleCount": len(baseline_samples),
                        "comparisonSampleCount": len(comparison_samples)}
