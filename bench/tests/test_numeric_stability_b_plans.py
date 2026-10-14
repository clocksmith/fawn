#!/usr/bin/env python3
"""Validation tests for B-track numeric-stability planning surfaces."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]

from bench.lib.config_validation import load_validated_config  # noqa: E402


class NumericStabilityBPlanTests(unittest.TestCase):
    def test_auto_detection_plan_validates_and_stays_runtime_first(self) -> None:
        plan = load_validated_config(
            REPO_ROOT / "config" / "numeric-stability-auto-detection-plan.json",
            REPO_ROOT / "config" / "numeric-stability-auto-detection-plan.schema.json",
        )
        self.assertFalse(plan["currentLiveBoundary"]["requiresAnnotation"])

    def test_operator_expansion_plan_validates(self) -> None:
        load_validated_config(
            REPO_ROOT / "config" / "numeric-stability-operator-expansion-plan.json",
            REPO_ROOT / "config" / "numeric-stability-operator-expansion-plan.schema.json",
        )


if __name__ == "__main__":
    unittest.main()
