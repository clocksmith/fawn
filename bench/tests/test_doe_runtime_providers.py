#!/usr/bin/env python3
"""Doe runtime CLI providers: a C runtime trace is Doe execution under its own
name in every gate that recognizes Doe traces, and never counts as Zig."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_ROOT = REPO_ROOT / "bench"
for _path_entry in (str(REPO_ROOT), str(BENCH_ROOT)):
    if _path_entry not in sys.path:
        sys.path.insert(0, _path_entry)

from bench.gates import claim_gate, structural_equivalence_gate
from bench.gates.claim_backend_telemetry import backend_telemetry_failures
from bench.lib import doe_runtime_providers
from bench.native_compare_modules import compare_assessment_helpers, comparability_runtime, run_artifact
from bench.native_compare_modules.executor_registry import resolve_executor_command_template

ZIG_META = {
    "executionBackend": "doe_vulkan",
    "backendId": "doe_vulkan",
    "backendSelectionReason": "policy_lane_prefers_doe_vulkan",
    "selectionPolicyHash": "backend-runtime-policy-v7",
    "fallbackUsed": False,
}
C_META = {
    "executionBackend": "doe_c_vulkan",
    "backendId": "doe_c_vulkan",
    "backendSelectionReason": "fixed_backend",
    "fallbackUsed": False,
}


def side(meta: dict) -> dict:
    return {"commandSamples": [{"returnCode": 0, "traceMeta": dict(meta)}]}


class DoeRuntimeProvidersTests(unittest.TestCase):
    def test_registry_names_one_provider_per_backend(self) -> None:
        zig = doe_runtime_providers.provider_for_execution_backend("doe_vulkan")
        c = doe_runtime_providers.provider_for_execution_backend("doe_c_vulkan")
        self.assertEqual((zig.runtime_implementation, zig.binary_name), ("zig", "doe-zig-runtime"))
        self.assertEqual((c.runtime_implementation, c.binary_name), ("c", "doe-c-runtime"))
        self.assertIsNone(doe_runtime_providers.provider_for_execution_backend("dawn_delegate"))
        self.assertEqual(
            doe_runtime_providers.native_execution_backends_for_api("vulkan"),
            frozenset({"doe_vulkan", "doe_c_vulkan"}),
        )

    def test_binary_detection_covers_both_clis(self) -> None:
        c_template = resolve_executor_command_template("doe_c_direct_vulkan")
        self.assertIn("runtime/c/runtime-cli/bin/doe-c-runtime", c_template)
        self.assertIn("--quirk-mode off", c_template)
        self.assertTrue(doe_runtime_providers.template_uses_doe_runtime(c_template))
        zig_template = resolve_executor_command_template("doe_direct_vulkan")
        self.assertTrue(doe_runtime_providers.template_uses_doe_runtime(zig_template))
        self.assertFalse(doe_runtime_providers.template_uses_doe_runtime("node bench/executors/run.js"))
        self.assertEqual(doe_runtime_providers.runtime_index(["env", "X=1", "/abs/doe-c-runtime"]), 2)

    def test_c_trace_is_doe_execution_in_dawn_comparisons(self) -> None:
        self.assertTrue(compare_assessment_helpers.uses_doe_execution({"doe_c_vulkan"}))
        workload = {"baseline": side({"executionBackend": "dawn_delegate"}), "comparison": side(C_META)}
        self.assertTrue(claim_gate.workload_is_dawn_vs_doe(workload))
        self.assertTrue(structural_equivalence_gate.workload_is_dawn_vs_doe(workload))
        self.assertIn("doe_c_vulkan", comparability_runtime._NATIVE_VULKAN_EXECUTION_BACKENDS)

    def test_strict_dawn_comparison_requires_c_shader_receipts(self) -> None:
        # The C CLI writes no shader artifact manifest: the obligation applies and fails.
        applies, passes, _, reason = comparability_runtime.assess_native_shader_artifact_equivalence(
            workload_api="vulkan",
            workload_commands_path="examples/workgroup_atomic_commands.json",
            comparability_mode="strict",
            is_dawn_vs_doe=True,
            left_execution_backends={"dawn_delegate"},
            right_execution_backends={"doe_c_vulkan"},
            left_command_samples=[{"traceMeta": {"executionBackend": "dawn_delegate"}}],
            right_command_samples=[{"traceMeta": dict(C_META)}],
        )
        self.assertEqual((applies, passes), (True, False))
        self.assertIn("Doe receipt has no shader manifest", reason)

    def test_zig_versus_c_is_not_a_dawn_comparison(self) -> None:
        workload = {"baseline": side(ZIG_META), "comparison": side(C_META)}
        self.assertFalse(claim_gate.workload_is_dawn_vs_doe(workload))

    def test_runtime_identity_names_the_implementation(self) -> None:
        for meta, implementation in ((ZIG_META, "zig"), (C_META, "c")):
            identity = run_artifact._runtime_identity(
                executor_id="x", samples=[{"command": [], "traceMeta": meta}]
            )
            self.assertEqual(identity["executionBackend"], meta["executionBackend"])
            self.assertEqual(identity["runtimeImplementation"], implementation)
        dawn = run_artifact._runtime_identity(
            executor_id="x", samples=[{"command": [], "traceMeta": {"executionBackend": "dawn_delegate"}}]
        )
        self.assertNotIn("runtimeImplementation", dawn)

    def test_selection_policy_hash_follows_the_provider(self) -> None:
        def failures(meta: dict, expected: str) -> list[str]:
            return backend_telemetry_failures(
                workload_id="w", workload={"baseline": side(meta)}, expected_backend_id=expected
            )

        self.assertEqual(failures(C_META, "doe_c_vulkan"), [])
        self.assertEqual(failures(ZIG_META, "doe_vulkan"), [])
        zig_without_policy = {k: v for k, v in ZIG_META.items() if k != "selectionPolicyHash"}
        self.assertEqual(failures(zig_without_policy, "doe_vulkan"), ["w: sample 0 missing selectionPolicyHash"])
        c_claiming_policy = {**C_META, "selectionPolicyHash": "backend-runtime-policy-v7"}
        self.assertEqual(len(failures(c_claiming_policy, "doe_c_vulkan")), 1)
        # Expecting the Zig backend rejects a C trace instead of relabeling it.
        self.assertEqual(len(failures(C_META, "doe_vulkan")), 1)


if __name__ == "__main__":
    unittest.main()
