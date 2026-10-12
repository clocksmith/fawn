"""Adversarial controls for the retained Intel projection evidence."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "reports/maintenance/20261011-intel-vulkan-projection"
SPEC = importlib.util.spec_from_file_location("projection_evidence", ROOT / "bench/executors/validate-projection-evidence.py")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ProjectionEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.matrix = json.loads((REPORT / "matrix.json").read_text())
        self.cohort = json.loads((REPORT / "cohort.json").read_text())
        self.policy_bytes = (REPORT / "policy.json").read_bytes()
        self.policy = json.loads(self.policy_bytes)

    def test_retained_decisions(self):
        MODULE.validate_matrix(self.matrix)
        MODULE.validate_cohort(self.cohort, self.policy, self.policy_bytes)
        self.assertFalse(self.cohort["accepted"])

    def test_dawn_recompilation_cannot_be_claimed_as_direct(self):
        row = next(arm for arm in self.matrix["arms"] if arm["runtime"] == "dawn" and arm["compiler"] == "doe")
        row["sequences"][0]["compilerProductPreserved"] = True
        with self.assertRaisesRegex(ValueError, "byte custody"):
            MODULE.validate_matrix(self.matrix)

    def test_wrong_output_cannot_keep_passing_verdict(self):
        row = next(arm for arm in self.matrix["arms"] if arm["runtime"] == "doe")
        sample = next(item for item in row["sequences"][0]["observations"] if item["kind"] == "sample")
        sample["exact"] = False
        with self.assertRaisesRegex(ValueError, "Exact complete"):
            MODULE.validate_matrix(self.matrix)

    def test_missing_child_cannot_be_discarded(self):
        self.cohort["rows"].pop(0)
        with self.assertRaisesRegex(ValueError, "predeclared child"):
            MODULE.validate_cohort(self.cohort, self.policy, self.policy_bytes)

    def test_frozen_rejection_cannot_be_promoted(self):
        self.cohort["accepted"] = True
        with self.assertRaisesRegex(ValueError, "acceptance decision"):
            MODULE.validate_cohort(self.cohort, self.policy, self.policy_bytes)

    def test_changed_policy_cannot_reuse_receipt(self):
        policy = copy.deepcopy(self.policy)
        policy["maximumPrimaryRatio"] = 1.1
        changed = json.dumps(policy).encode()
        with self.assertRaisesRegex(ValueError, "policy identity"):
            MODULE.validate_cohort(self.cohort, policy, changed)


if __name__ == "__main__":
    unittest.main()
