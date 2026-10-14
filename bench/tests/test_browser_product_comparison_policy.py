#!/usr/bin/env python3
"""Semantic contract tests for browser product comparison policy."""

from __future__ import annotations

import copy
import json
import unittest
from pathlib import Path
from typing import Any
from jsonschema import Draft202012Validator

REPO_ROOT = Path(__file__).resolve().parents[2]
POLICY_PATH = REPO_ROOT / "config" / "browser-product-comparison-policy.json"
STRATEGY_PATH = REPO_ROOT / "config" / "doe-product-strategy.json"


def _load_policy() -> dict[str, Any]:
    """Load the governed browser product comparison policy."""
    return json.loads(POLICY_PATH.read_text(encoding="utf-8"))


class BrowserProductComparisonPolicyTests(unittest.TestCase):
    """Protect causal lanes, external comparison, and fork boundaries."""

    def test_runtime_primary_and_feature_role_are_schema_enforced(self) -> None:
        strategy = json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))
        schema_path = STRATEGY_PATH.with_suffix(".schema.json")
        validator = Draft202012Validator(
            json.loads(schema_path.read_text(encoding="utf-8"))
        )
        validator.validate(strategy)
        surfaces = {row["id"]: row for row in strategy["surfaces"]}
        surfaces["fawn"]["portfolioRole"] = "secondary-product"
        self.assertFalse(validator.is_valid(strategy))
        surfaces["fawn"]["portfolioRole"] = "primary-product"
        surfaces["doeproof"]["portfolioRole"] = "primary-product"
        self.assertFalse(validator.is_valid(strategy))

    def test_strategy_rejects_implicit_expansion_and_coupled_adoption(
        self,
    ) -> None:
        strategy = json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))
        validator = Draft202012Validator(
            json.loads(
                STRATEGY_PATH.with_suffix(".schema.json").read_text(
                    encoding="utf-8"
                )
            )
        )
        validator.validate(strategy)
        cases = []
        missing_compiler = copy.deepcopy(strategy)
        missing_compiler["surfaces"] = [
            row
            for row in missing_compiler["surfaces"]
            if row["id"] != "doecompiler"
        ]
        cases.append(missing_compiler)
        coupled = copy.deepcopy(strategy)
        compiler = next(
            row for row in coupled["surfaces"] if row["id"] == "doecompiler"
        )
        compiler["marketRole"] = "runtime-adoption"
        cases.append(coupled)
        for key, value in (
            ("activeBackend", "apple-metal"),
            ("metalTransition", "customer-request"),
            ("d3d12", "active"),
            ("browser", "required-destination"),
        ):
            expanded = copy.deepcopy(strategy)
            expanded["executionOwnership"]["workSelection"][key] = value
            cases.append(expanded)
        for index, candidate in enumerate(cases):
            with self.subTest(case=index):
                self.assertFalse(validator.is_valid(candidate))

    def test_strategy_preserves_comparison_classes_and_acceptance_order(
        self,
    ) -> None:
        strategy = json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))
        validator = Draft202012Validator(
            json.loads(
                STRATEGY_PATH.with_suffix(".schema.json").read_text(
                    encoding="utf-8"
                )
            )
        )
        classes = strategy["comparisonLaw"]["classes"]
        for comparison_class in classes:
            candidate = copy.deepcopy(strategy)
            candidate["comparisonLaw"]["classes"].remove(comparison_class)
            self.assertFalse(validator.is_valid(candidate))
        strategy["milestones"][0], strategy["milestones"][1] = (
            strategy["milestones"][1],
            strategy["milestones"][0],
        )
        self.assertFalse(validator.is_valid(strategy))

    def test_qualification_cannot_replace_adoption_and_repeat_retention(
        self,
    ) -> None:
        strategy = json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))
        schema = json.loads(
            STRATEGY_PATH.with_suffix(".schema.json").read_text(
                encoding="utf-8"
            )
        )
        validator = Draft202012Validator(schema)
        gate = strategy["commercialOffer"]["commercialPromotionGate"]
        original = gate["requiredEvidence"]
        for field in original:
            gate["requiredEvidence"] = [
                item for item in original if item != field
            ]
            self.assertFalse(validator.is_valid(strategy), field)
        gate["requiredEvidence"] = ["paid-qualification"]
        self.assertFalse(validator.is_valid(strategy))

    def test_external_comparators_are_not_component_substitutions(self) -> None:
        policy = _load_policy()
        internal_ids = {
            lane["laneId"] for lane in policy["internalCausalLanes"]
        }

        for comparator in policy["externalComparators"]:
            with self.subTest(comparator=comparator["comparatorId"]):
                self.assertNotIn(comparator["comparatorId"], internal_ids)
                self.assertFalse(comparator["componentSubstitution"])

    def test_suites_require_total_outcome_and_compatibility_failures(self) -> None:
        policy = _load_policy()

        for suite in policy["suites"]:
            self.assertIn("total task outcome", suite["requiredObservations"])
            self.assertIn(
                "compatibility failures", suite["requiredObservations"]
            )

    def test_complete_system_alternative_cannot_claim_component_attribution(
        self,
    ) -> None:
        policy = _load_policy()
        validator = Draft202012Validator(
            json.loads(POLICY_PATH.with_suffix(".schema.json").read_text(encoding="utf-8"))
        )
        validator.validate(policy)
        section = policy["alternativeCompleteSystem"]
        self.assertTrue((REPO_ROOT / section["alternatives"][0]["treatmentModelPath"]).is_file())
        mutations = [
            lambda value: value.__setitem__("isolatesDoeCompiler", True),
            lambda value: value.__setitem__("isolatesDoeRuntime", True),
            lambda value: value["matchedConditions"].remove("resource-budgets"),
            lambda value: value["recordedDifferences"].remove("quantization"),
            lambda value: value["alternatives"][0]["model"].pop("sha256"),
            lambda value: value["alternatives"][1].__setitem__("admissionState", "pinned-not-run"),
            lambda value: value["alternatives"][1].__setitem__("admissionState", "admitted"),
        ]
        for mutate in mutations:
            candidate = copy.deepcopy(policy)
            mutate(candidate["alternativeCompleteSystem"])
            self.assertFalse(validator.is_valid(candidate))

    def test_initial_target_stays_unqualified_and_comparisons_stay_separate(
        self,
    ) -> None:
        strategy = json.loads(STRATEGY_PATH.read_text(encoding="utf-8"))
        validator = Draft202012Validator(
            json.loads(STRATEGY_PATH.with_suffix(".schema.json").read_text(encoding="utf-8"))
        )
        validator.validate(strategy)
        target = strategy["platformSequence"][0]["initialQualificationTarget"]
        self.assertTrue((REPO_ROOT / target["hostRecordPath"]).is_file())
        mutations = [
            lambda value: value["platformSequence"][0]["initialQualificationTarget"].__setitem__(
                "qualificationState", "qualified"
            ),
            lambda value: value["platformSequence"][0]["initialQualificationTarget"].__setitem__(
                "memoryAccounting", ["system-ram", "gpu-allocation-limits"]
            ),
            lambda value: value["comparisonLaw"]["categories"][1].__setitem__(
                "isolatesProvider", True
            ),
            lambda value: value["comparisonLaw"]["categories"].pop(),
        ]
        for mutate in mutations:
            candidate = copy.deepcopy(strategy)
            mutate(candidate)
            self.assertFalse(validator.is_valid(candidate))

    def test_evidence_references_exist(self) -> None:
        policy = _load_policy()

        for reference in policy["evidenceReferences"]:
            self.assertTrue((REPO_ROOT / reference).exists(), reference)


if __name__ == "__main__":
    unittest.main()
