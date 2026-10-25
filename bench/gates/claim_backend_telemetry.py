"""Backend telemetry checks used by the release claim gate."""

from __future__ import annotations

from typing import Any

from bench.lib import doe_runtime_providers


def backend_telemetry_failures(
    *,
    workload_id: str,
    workload: dict[str, Any],
    expected_backend_id: str,
) -> list[str]:
    failures: list[str] = []
    left_payload = workload.get("baseline")
    if not isinstance(left_payload, dict):
        return [f"{workload_id}: missing baseline payload for backend telemetry checks"]

    command_samples = left_payload.get("commandSamples")
    if not isinstance(command_samples, list) or not command_samples:
        return [
            f"{workload_id}: missing baseline.commandSamples for backend telemetry checks"
        ]

    for sample_idx, sample in enumerate(command_samples):
        if not isinstance(sample, dict):
            continue
        if sample.get("returnCode") != 0:
            continue
        trace_meta = sample.get("traceMeta")
        if not isinstance(trace_meta, dict):
            failures.append(
                f"{workload_id}: sample {sample_idx} missing traceMeta for "
                "backend telemetry checks"
            )
            continue
        backend_id = trace_meta.get("backendId")
        if not isinstance(backend_id, str) or not backend_id:
            failures.append(f"{workload_id}: sample {sample_idx} missing backendId")
        elif expected_backend_id and backend_id != expected_backend_id:
            failures.append(
                f"{workload_id}: sample {sample_idx} backendId mismatch "
                f"expected={expected_backend_id} got={backend_id}"
            )
        selection_reason = trace_meta.get("backendSelectionReason")
        if not isinstance(selection_reason, str) or not selection_reason:
            failures.append(
                f"{workload_id}: sample {sample_idx} missing backendSelectionReason"
            )
        # A fixed-backend runtime CLI (config/doe-runtime-providers.json) has no
        # lane policy to hash; every other backend must name the policy it used.
        selection_policy_hash = trace_meta.get("selectionPolicyHash")
        policy_required = doe_runtime_providers.requires_selection_policy(str(backend_id or ""))
        if policy_required and (not isinstance(selection_policy_hash, str) or not selection_policy_hash):
            failures.append(
                f"{workload_id}: sample {sample_idx} missing selectionPolicyHash"
            )
        elif not policy_required and selection_policy_hash is not None:
            failures.append(
                f"{workload_id}: sample {sample_idx} fixed-backend {backend_id} "
                "must not report a selectionPolicyHash"
            )
        fallback_used = trace_meta.get("fallbackUsed")
        if not isinstance(fallback_used, bool):
            failures.append(
                f"{workload_id}: sample {sample_idx} missing fallbackUsed bool"
            )
    return failures
