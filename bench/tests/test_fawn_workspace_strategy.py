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


ADVANTAGED = "advantaged-on-frozen-differentiation-suite"


def differentiation_failures(strategy: dict[str, Any], result: dict[str, Any]) -> list[str]:
    """Step 5's advantage needs a full result for the named commit whose frozen rule passed."""
    evidence = strategy["fawnWorkspace"]["differentiationEvidence"]
    suite = load_json("config/browser-product-comparison-policy.json")["workspaceComparison"]["differentiation"]
    failures = []
    if result["scope"] != "full" or result["suiteVersion"] != suite["suiteVersion"]:
        failures.append("result is not a full run of the frozen suite")
    if result["identity"]["git"]["commit"] != evidence["sourceCommit"] or result["identity"]["git"]["workingTreeChanges"]:
        failures.append("result ran other or uncommitted source")
    advantage = result["productAdvantage"]
    rule = suite["productAdvantageRule"]
    if not advantage["established"] or len(advantage["areasMet"]) < rule["minimumAreasMet"]:
        failures.append("the frozen product-advantage rule did not pass")
    if advantage["suiteRegressions"] or advantage["scriptedRegressions"] != []:
        failures.append("a material regression was recorded")
    frozen = [(entry["area"], entry["scenario"]) for entry in suite["improvementRequirements"]]
    if [(entry["area"], entry["scenario"]) for entry in result["requirements"]] != frozen:
        failures.append("result requirements differ from the frozen ones")
    if not result["sameBrowserBinary"] or result["debuggingEndpoints"] != 0:
        failures.append("unequal browser or an exposed debugging endpoint")
    scripted = load_json(evidence["scriptedComparisonPath"])
    if scripted["identity"]["git"]["commit"] != evidence["sourceCommit"] or scripted["scope"] != "full":
        failures.append("scripted comparison ran other source or a subset")
    return failures


def stronger_control_failures(strategy: dict[str, Any], result: dict[str, Any]) -> list[str]:
    """The second suite must have run in full on the named commit with its frozen rule passed."""
    evidence = strategy["fawnWorkspace"]["strongerControlEvidence"]
    comparison = load_json("config/browser-product-comparison-policy.json")["workspaceComparison"]
    suite = comparison["revalidatedDifferentiation"]
    failures = []
    if result["suite"] != "revalidatedDifferentiation" or result["scope"] != "full" or result["suiteVersion"] != suite["suiteVersion"]:
        failures.append("result is not a full run of the second frozen suite")
    if result["identity"]["git"]["commit"] != evidence["sourceCommit"] or result["identity"]["git"]["workingTreeChanges"]:
        failures.append("result ran other or uncommitted source")
    if not result["productAdvantage"]["established"] or result["productAdvantage"]["suiteRegressions"]:
        failures.append("the second suite's frozen rule did not pass")
    frozen = [(entry["area"], entry["scenario"]) for entry in suite["improvementRequirements"]]
    if [(entry["area"], entry["scenario"]) for entry in result["requirements"]] != frozen:
        failures.append("result requirements differ from the frozen ones")
    first = comparison["differentiation"]["suiteVersion"]
    priors = result["productAdvantage"]["priorRegressions"]
    if not any(prior["suiteVersion"] == first and prior["full"] and not prior["regressions"] for prior in priors):
        failures.append("the first suite was not a clean prior")
    if not result["sameBrowserBinary"] or result["debuggingEndpoints"] != 0:
        failures.append("unequal browser or an exposed debugging endpoint")
    return failures


def compatibility_failures(strategy: dict[str, Any]) -> list[str]:
    """Each compatibility build must be a full admitted run of the frozen case set on its channel."""
    evidence = strategy["fawnWorkspace"]["compatibilityEvidence"]
    case_set = load_json("config/fawn-workspace-qualification.json")
    failures = []
    for build in evidence["builds"]:
        verdict = load_json(build["verdictPath"])
        result = load_json(build["verdictPath"].replace("verdict.json", "result.json"))
        if not verdict["admitted"] or verdict["caseSetVersion"] != case_set["caseSetVersion"]:
            failures.append(f"{build['channel']}: not an admitted run of the frozen case set")
        if verdict["identity"]["git"]["commit"] != build["sourceCommit"] or verdict["identity"]["git"]["workingTreeChanges"]:
            failures.append(f"{build['channel']}: ran another or uncommitted commit")
        if result["browserChannel"] != build["channel"] or result.get("headless", True) != build["headless"]:
            failures.append(f"{build['channel']}: browser channel or headless mode differs")
        if not all(verdict["browsers"].values()):
            failures.append(f"{build['channel']}: browser identity missing")
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
            [ESTABLISHED] * 4 + [ADVANTAGED, "unestablished"],
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

    def test_product_advantage_binds_to_the_frozen_differentiation_result(self) -> None:
        strategy = load_strategy()
        evidence = strategy["fawnWorkspace"]["differentiationEvidence"]
        result = load_json(evidence["resultPath"])
        self.assertEqual(differentiation_failures(strategy, result), [])
        for tamper in (
            lambda copy: copy["productAdvantage"].update(established=False),
            lambda copy: copy["identity"]["git"].update(commit="0" * 40),
            lambda copy: copy["productAdvantage"].update(scriptedRegressions=None),
            lambda copy: copy["requirements"].pop(),
        ):
            altered = copy.deepcopy(result)
            tamper(altered)
            self.assertTrue(differentiation_failures(strategy, altered))

    def test_stronger_control_and_compatibility_bind_to_their_results(self) -> None:
        strategy = load_strategy()
        result = load_json(strategy["fawnWorkspace"]["strongerControlEvidence"]["resultPath"])
        self.assertEqual(stronger_control_failures(strategy, result), [])
        self.assertEqual(compatibility_failures(strategy), [])
        for tamper in (
            lambda copy: copy.update(suite="differentiation"),
            lambda copy: copy["productAdvantage"].update(priorRegressions=[]),
            lambda copy: copy["identity"]["git"].update(commit="0" * 40),
        ):
            altered = copy.deepcopy(result)
            tamper(altered)
            self.assertTrue(stronger_control_failures(strategy, altered))

    def test_model_driven_freeze_matches_its_pinned_artifacts(self) -> None:
        suite = load_json("config/browser-product-comparison-policy.json")["workspaceComparison"]["modelDrivenDifferentiation"]
        for artifact in suite["frozenArtifacts"]:
            with self.subTest(path=artifact["path"]):
                digest = hashlib.sha256((REPO_ROOT / artifact["path"]).read_bytes()).hexdigest()
                self.assertEqual(digest, artifact["sha256"], "a frozen prompt, tool description or held-out specification changed")

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
