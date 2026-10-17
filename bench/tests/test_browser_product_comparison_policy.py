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

    def test_strategy_keeps_independent_surfaces_and_unestablished_proofs(
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
        established = copy.deepcopy(strategy)
        established["roadmapProofs"]["proofs"][2]["currentAssessment"] = "established"
        cases.append(established)
        without_stock_arm = copy.deepcopy(strategy)
        without_stock_arm["roadmapProofs"]["proofs"][2]["arms"].remove("stock-chromium-with-dawn")
        cases.append(without_stock_arm)
        reordered = copy.deepcopy(strategy)
        reordered["roadmapProofs"]["proofs"].reverse()
        cases.append(reordered)
        for index, candidate in enumerate(cases):
            with self.subTest(case=index):
                self.assertFalse(validator.is_valid(candidate))

    def test_strategy_preserves_comparison_classes(self) -> None:
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

if __name__ == "__main__":
    unittest.main()
