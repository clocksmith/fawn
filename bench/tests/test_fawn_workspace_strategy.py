"""Protect workspace status: assessments come only from admitted evidence."""

from __future__ import annotations

import copy
import hashlib
import json
import unittest
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator


REPO_ROOT = Path(__file__).resolve().parents[2]
STRATEGY_PATH = REPO_ROOT / "config/doe-product-strategy.json"


def load_strategy() -> dict[str, Any]:
    return json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))


def load_json(relative: str) -> dict[str, Any]:
    return json.loads((REPO_ROOT / relative).read_text(encoding="utf-8"))


ESTABLISHED = "established-on-frozen-case-set"


def binding_failures(strategy: dict[str, Any], verdict: dict[str, Any]) -> list[str]:
    """Every requirement of an established step must be established in the admitted verdict."""
    workspace = strategy["fawnWorkspace"]
    evidence = workspace["qualificationEvidence"]
    case_set_path = evidence["caseSetPath"]
    case_set = load_json(case_set_path)
    failures = []
    if not verdict["admitted"] or verdict["scope"] != "full" or verdict["failures"]:
        failures.append("verdict is not a full admitted result")
    if verdict["identity"]["git"]["commit"] != evidence["sourceCommit"]:
        failures.append("verdict ran a different commit")
    if verdict["identity"]["git"]["workingTreeChanges"]:
        failures.append("verdict ran uncommitted source")
    current = hashlib.sha256((REPO_ROOT / case_set_path).read_bytes()).hexdigest()
    if verdict["identity"]["caseSet"]["sha256"] != current or verdict["caseSetVersion"] != case_set["caseSetVersion"]:
        failures.append("verdict ran a different case set than the frozen one")
    statuses = {entry["id"]: entry["status"] for entry in verdict["requirements"]}
    for step in workspace["steps"]:
        if step["currentAssessment"] != ESTABLISHED:
            continue
        for requirement in step["requiredEvidence"]:
            if statuses.get(requirement) != "established":
                failures.append(f"{step['id']}: {requirement} is {statuses.get(requirement)}")
    return failures


def validator() -> Draft202012Validator:
    schema = json.loads(
        STRATEGY_PATH.with_suffix(".schema.json").read_text(encoding="utf-8")
    )
    return Draft202012Validator(schema)


class FawnWorkspaceStrategyTests(unittest.TestCase):
    """These are policy checks; runtime admission remains blocked."""

    def test_ordered_workspace_steps_have_one_canonical_owner(self) -> None:
        strategy = load_strategy()
        validator().validate(strategy)
        workspace = strategy["fawnWorkspace"]
        steps = workspace["steps"]
        step_ids = [step["id"] for step in steps]
        for index, step in enumerate(steps):
            self.assertEqual(step["order"], index + 1)
            self.assertEqual(
                step["dependsOn"], [] if index == 0 else [step_ids[index - 1]]
            )
        for field in ("contractPath", "statusPath", "charterPath"):
            self.assertTrue((REPO_ROOT / workspace[field]).is_file(), field)
        for field in ("implementationPath", "developmentTestsPath"):
            self.assertTrue((REPO_ROOT / workspace[field]).is_dir(), field)
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

    def test_status_cannot_exceed_admitted_evidence(self) -> None:
        strategy = load_strategy()
        workspace = strategy["fawnWorkspace"]
        self.assertEqual(
            workspace["implementationStatus"], "partially-implemented-behavior-qualified"
        )
        self.assertEqual(
            [step["implementationStatus"] for step in workspace["steps"]],
            ["implemented-behavior-qualified"] * 4 + ["implemented-compared", "not-implemented"],
        )
        self.assertEqual(
            [step["currentAssessment"] for step in workspace["steps"]],
            [ESTABLISHED] * 4 + ["unestablished"] * 2,
        )
        self.assertEqual(workspace["currentAssessment"], "unestablished")
        self.assertEqual(
            workspace["releaseAdmission"],
            "blocked-until-implemented-and-independently-qualified",
        )
        check = validator()
        for target in [workspace, *workspace["steps"]]:
            for field, value in (
                ("implementationStatus", "implemented"),
                ("implementationStatus", "qualified"),
                ("currentAssessment", "claimable"),
            ):
                original = target[field]
                target[field] = value
                self.assertFalse(check.is_valid(strategy))
                target[field] = original
        for step in workspace["steps"][4:]:
            original = step["currentAssessment"]
            step["currentAssessment"] = ESTABLISHED
            self.assertFalse(check.is_valid(strategy))
            step["currentAssessment"] = original
        workspace["releaseAdmission"] = "qualified"
        self.assertFalse(check.is_valid(strategy))

    def test_established_steps_bind_to_the_admitted_verdict(self) -> None:
        strategy = load_strategy()
        evidence = strategy["fawnWorkspace"]["qualificationEvidence"]
        verdict = load_json(evidence["admittedVerdictPath"])
        self.assertEqual(binding_failures(strategy, verdict), [])
        comparison = load_json(evidence["comparisonPath"])
        policy = load_json("config/browser-product-comparison-policy.json")
        self.assertEqual(comparison["identity"]["git"]["commit"], evidence["sourceCommit"])
        self.assertEqual(
            comparison["comparisonVersion"],
            policy["workspaceComparison"]["comparisonVersion"],
        )
        self.assertEqual(comparison["scope"], "full")
        self.assertTrue(comparison["equalOpportunityChecks"]["sameBrowserBinary"])

    def test_binding_rejects_evidence_that_does_not_establish_a_step(self) -> None:
        strategy = load_strategy()
        evidence = strategy["fawnWorkspace"]["qualificationEvidence"]
        verdict = load_json(evidence["admittedVerdictPath"])
        failed = copy.deepcopy(verdict)
        failed["requirements"][0]["status"] = "failed"
        self.assertTrue(binding_failures(strategy, failed))
        other_commit = copy.deepcopy(verdict)
        other_commit["identity"]["git"]["commit"] = "0" * 40
        self.assertTrue(binding_failures(strategy, other_commit))
        not_admitted = copy.deepcopy(verdict)
        not_admitted["admitted"] = False
        self.assertTrue(binding_failures(strategy, not_admitted))
        overclaim = copy.deepcopy(strategy)
        overclaim["fawnWorkspace"]["steps"][4]["currentAssessment"] = ESTABLISHED
        self.assertTrue(binding_failures(overclaim, verdict))

    def test_workspace_is_independent_of_runtime_wins_and_keeps_native_gates(
        self,
    ) -> None:
        strategy = load_strategy()
        workspace = strategy["fawnWorkspace"]
        self.assertFalse(workspace["requiresNativeRuntimeWins"])
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
        self.assertFalse(comparison["requiresNativeRuntimeWins"])
        comparison["treatment"] = "existing-gpu-recovery-receipts"
        self.assertFalse(check.is_valid(policy))


if __name__ == "__main__":
    unittest.main()
