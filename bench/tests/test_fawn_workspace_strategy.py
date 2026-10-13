"""Protect prospective workspace authority without claiming a service exists."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator


REPO_ROOT = Path(__file__).resolve().parents[2]
STRATEGY_PATH = REPO_ROOT / "config/doe-product-strategy.json"
STEP_IDS = [
    "shared-workspace-service",
    "observation-action-binding",
    "durable-ownership",
    "uncertainty-recovery",
    "reuse-and-product-value",
    "selective-deeper-capabilities",
]


def load_strategy() -> dict[str, Any]:
    return json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))


def validator() -> Draft202012Validator:
    schema = json.loads(
        STRATEGY_PATH.with_suffix(".schema.json").read_text(encoding="utf-8")
    )
    return Draft202012Validator(schema)


class FawnWorkspaceStrategyTests(unittest.TestCase):
    """These are intent checks; runtime admission remains absent and blocked."""

    def test_ordered_workspace_steps_have_one_canonical_owner(self) -> None:
        strategy = load_strategy()
        validator().validate(strategy)
        workspace = strategy["fawnWorkspace"]
        steps = workspace["steps"]
        self.assertEqual([step["id"] for step in steps], STEP_IDS)
        for index, step in enumerate(steps):
            self.assertEqual(step["order"], index + 1)
            self.assertEqual(
                step["dependsOn"], [] if index == 0 else [STEP_IDS[index - 1]]
            )
        for field in ("contractPath", "statusPath", "charterPath"):
            self.assertTrue((REPO_ROOT / workspace[field]).is_file(), field)
        reversed_steps = copy.deepcopy(strategy)
        reversed_steps["fawnWorkspace"]["steps"].reverse()
        self.assertFalse(validator().is_valid(reversed_steps))

    def test_missing_safety_or_reuse_requirement_is_rejected(self) -> None:
        strategy = load_strategy()
        check = validator()
        for index, step in enumerate(strategy["fawnWorkspace"]["steps"]):
            for requirement in step["requiredEvidence"]:
                with self.subTest(step=step["id"], requirement=requirement):
                    candidate = copy.deepcopy(strategy)
                    candidate["fawnWorkspace"]["steps"][index][
                        "requiredEvidence"
                    ].remove(requirement)
                    self.assertFalse(check.is_valid(candidate))

    def test_contract_cannot_fabricate_implemented_or_qualified_state(self) -> None:
        strategy = load_strategy()
        workspace = strategy["fawnWorkspace"]
        self.assertEqual(workspace["implementationStatus"], "not-implemented")
        self.assertEqual(workspace["currentAssessment"], "unestablished")
        self.assertEqual(
            workspace["releaseAdmission"],
            "blocked-until-implemented-and-independently-qualified",
        )
        check = validator()
        for target in [workspace, *workspace["steps"]]:
            for field, value in (
                ("implementationStatus", "implemented"),
                ("currentAssessment", "claimable"),
            ):
                original = target[field]
                target[field] = value
                self.assertFalse(check.is_valid(strategy))
                target[field] = original
        workspace["releaseAdmission"] = "qualified"
        self.assertFalse(check.is_valid(strategy))

    def test_workspace_is_independent_of_runtime_wins_and_keeps_native_gates(
        self,
    ) -> None:
        strategy = load_strategy()
        workspace = strategy["fawnWorkspace"]
        self.assertFalse(workspace["requiresNativeRuntimeWins"])
        self.assertEqual(
            workspace["initialIntegration"], "stock-chromium-cdp-playwright"
        )
        self.assertEqual(
            [milestone["id"] for milestone in strategy["milestones"]],
            [
                "provider-qualification-v1",
                "framework-substitution-v1",
                "application-transfer-v1",
                "chromium-substitution-v1",
            ],
        )
        work_selection = strategy["executionOwnership"]["workSelection"]
        self.assertEqual(work_selection["activeBackend"], "amd-vulkan")
        self.assertEqual(work_selection["d3d12"], "deferred")
        self.assertEqual(
            work_selection["browser"], "explicit-selection-required"
        )
        workspace["requiresNativeRuntimeWins"] = True
        self.assertFalse(validator().is_valid(strategy))

    def test_independent_workflow_comparison_cannot_relabel_legacy_lanes(
        self,
    ) -> None:
        policy_path = REPO_ROOT / "config/browser-product-comparison-policy.json"
        policy = json.loads(policy_path.read_text(encoding="utf-8"))
        schema = json.loads(
            policy_path.with_suffix(".schema.json").read_text(encoding="utf-8")
        )
        check = Draft202012Validator(schema)
        check.validate(policy)
        comparison = policy["workspaceComparison"]
        self.assertEqual(
            comparison["control"], "competent-stock-chromium-playwright"
        )
        self.assertEqual(
            comparison["treatment"],
            "same-browser-and-runtime-with-fawn-workspace-service",
        )
        self.assertFalse(comparison["requiresNativeRuntimeWins"])
        self.assertEqual(
            [lane["laneId"] for lane in policy["internalCausalLanes"]],
            ["A", "B", "C", "D"],
        )
        comparison["treatment"] = "existing-gpu-recovery-receipts"
        self.assertFalse(check.is_valid(policy))


if __name__ == "__main__":
    unittest.main()
