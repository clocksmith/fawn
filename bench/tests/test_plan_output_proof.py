"""Same-execution plan proof admission and stale/isolated evidence rejection."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import tempfile
import struct
import unittest

from jsonschema import Draft202012Validator, ValidationError

from bench.native_compare_modules.plan_output_proof import assess_plan_output_proof


class PlanOutputProofTests(unittest.TestCase):
    def setUp(self) -> None:
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "plan.json"
        self.oracle = {
            "schema_version": 2, "scope": "command_graph", "reference_class": "independent_v1",
            "kind": "sha256_exact_v1", "initialization": "zero_fill_v1",
            "binding_group": 0, "binding": 1, "dispatch_count": 1,
            "expected_sha256": "a" * 64, "reference_id": "independent-oracle",
        }
        self.plan = {
            "planSha256": "b" * 64,
            "commands": [{"kind": "kernel_dispatch", "kernel": "scale", "entryPoint": "main",
                          "bindings": [{"group": 0, "binding": 1, "resourceHandle": 9, "bufferSize": 32}],
                          "output_oracle": self.oracle}],
        }
        self.path.write_text(json.dumps(self.plan))
        self.sample = {"returnCode": 0, "success": True, "traceMeta": {
            "hostPlanArtifactPath": str(self.path), "hostPlanArtifactHash": "b" * 64,
            "outputOracleCount": 1, "outputOracleMatchedCount": 1, "outputOracleFailedCount": 0,
            "executionErrorCount": 0, "executionUnsupportedCount": 0, "executionSkippedCount": 0,
            "timedOutputProof": {
                "schema": "doe.plan-output-proof/v1", "executionScope": "same-execution",
                "planSha256": "b" * 64, "planFileSha256": hashlib.sha256(self.path.read_bytes()).hexdigest(),
                "observations": [{"commandSeq": 0, "kernel": "scale", "entryPoint": "main",
                                  "bindingGroup": 0, "binding": 1, "resourceHandle": 9,
                                  "byteLength": 32, "dispatchCount": 1, "actualSha256": "a" * 64,
                                  "matched": True, "comparedValueCount": 0, "mismatchCount": 0,
                                  "oracle": {"kind": "sha256_exact_v1", "referenceClass": "independent_v1",
                                             "expectedSha256": "a" * 64, "referenceId": "independent-oracle",
                                             "referencePath": None, "absoluteTolerance": 0, "relativeTolerance": 0}}],
            },
        }}

    def assess(self, changed: dict | None = None) -> tuple[bool, dict]:
        return assess_plan_output_proof([self.sample], [changed or deepcopy(self.sample)])

    def test_trace_schema_declares_and_bounds_the_new_proof(self) -> None:
        schema_path = Path(__file__).resolve().parents[2] / "config/trace-meta.schema.json"
        schema = json.loads(schema_path.read_text())["properties"]["timedOutputProof"]
        validator = Draft202012Validator(schema)
        proof = deepcopy(self.sample["traceMeta"]["timedOutputProof"])
        validator.validate(proof)
        proof["executionScope"] = "isolated-validation"
        with self.assertRaises(ValidationError):
            validator.validate(proof)
        proof["executionScope"] = "same-execution"
        proof["unrecognized"] = True
        with self.assertRaises(ValidationError):
            validator.validate(proof)

    def test_accepts_complete_proof_on_every_measured_sample(self) -> None:
        self.assertTrue(self.assess()[0])
        result = assess_plan_output_proof([self.sample] * 2, [deepcopy(self.sample)] * 2)
        self.assertTrue(result[0], result[1])

    def test_count_only_or_old_isolated_receipts_remain_rejected(self) -> None:
        for mode in ("missing", "isolated-validation", "replay"):
            changed = deepcopy(self.sample)
            if mode == "missing":
                changed["traceMeta"].pop("timedOutputProof")
            else:
                changed["traceMeta"]["timedOutputProof"]["executionScope"] = mode
            self.assertFalse(self.assess(changed)[0])

    def test_command_and_resource_substitutions_are_rejected(self) -> None:
        for key, value in (("commandSeq", 1), ("kernel", "other"), ("entryPoint", "other"),
                           ("bindingGroup", 1), ("binding", 0), ("resourceHandle", 8),
                           ("byteLength", 16), ("dispatchCount", 2)):
            with self.subTest(key=key):
                changed = deepcopy(self.sample)
                changed["traceMeta"]["timedOutputProof"]["observations"][0][key] = value
                self.assertFalse(self.assess(changed)[0])

    def test_reference_and_tolerance_substitutions_are_rejected(self) -> None:
        for key, value in (("expectedSha256", "c" * 64), ("referenceId", "other"),
                           ("referenceClass", "cross_runtime_consensus_v1"),
                           ("absoluteTolerance", 0.1), ("relativeTolerance", 0.1)):
            changed = deepcopy(self.sample)
            changed["traceMeta"]["timedOutputProof"]["observations"][0]["oracle"][key] = value
            self.assertFalse(self.assess(changed)[0])

    def test_missing_duplicate_or_failed_output_is_rejected(self) -> None:
        for mode in ("missing", "duplicate", "failed", "wrong-hash", "bool-count"):
            changed = deepcopy(self.sample)
            observations = changed["traceMeta"]["timedOutputProof"]["observations"]
            if mode == "missing":
                observations.clear()
            elif mode == "duplicate":
                observations.append(deepcopy(observations[0]))
            elif mode == "failed":
                observations[0]["matched"] = False
            elif mode == "bool-count":
                changed["traceMeta"]["outputOracleCount"] = True
            else:
                observations[0]["actualSha256"] = "c" * 64
            self.assertFalse(self.assess(changed)[0])

    def test_rejects_one_stale_sample_or_mutated_plan_bytes(self) -> None:
        stale = deepcopy(self.sample)
        stale["traceMeta"]["timedOutputProof"]["planFileSha256"] = "d" * 64
        self.assertFalse(assess_plan_output_proof([self.sample], [self.sample, stale])[0])
        self.path.write_text(self.path.read_text() + "\n")
        self.assertFalse(self.assess()[0])

    def test_rejects_proof_bound_to_another_plan_identity(self) -> None:
        changed = deepcopy(self.sample)
        changed["traceMeta"]["hostPlanArtifactHash"] = "e" * 64
        self.assertFalse(self.assess(changed)[0])

    def use_float_reference(self) -> Path:
        reference = self.path.with_name("reference.f32le.bin")
        reference.write_bytes(struct.pack("<8f", *range(8)))
        digest = hashlib.sha256(reference.read_bytes()).hexdigest()
        self.oracle.update(schema_version=3, kind="float32_reference_tolerance_v1",
                           reference_sha256=digest, reference_path=str(reference),
                           absolute_tolerance=1e-6, relative_tolerance=1e-5)
        self.oracle.pop("expected_sha256")
        self.path.write_text(json.dumps(self.plan))
        proof = self.sample["traceMeta"]["timedOutputProof"]
        proof["planFileSha256"] = hashlib.sha256(self.path.read_bytes()).hexdigest()
        observation = proof["observations"][0]
        observation.update(actualSha256=digest, comparedValueCount=8)
        observation["oracle"].update(
            kind="float32_reference_tolerance_v1", expectedSha256=digest,
            referencePath=str(reference),
            absoluteTolerance=struct.unpack("<f", struct.pack("<f", 1e-6))[0],
            relativeTolerance=struct.unpack("<f", struct.pack("<f", 1e-5))[0],
        )
        return reference

    def test_float_reference_requires_complete_pinned_independent_output(self) -> None:
        reference = self.use_float_reference()
        self.assertTrue(self.assess()[0])
        # JSON may encode the same f32 through its shortest decimal spelling.
        self.sample["traceMeta"]["timedOutputProof"]["observations"][0]["oracle"].update(
            absoluteTolerance=1e-6, relativeTolerance=1e-5)
        self.assertTrue(self.assess()[0])
        changed = deepcopy(self.sample)
        changed["traceMeta"]["timedOutputProof"]["observations"][0]["comparedValueCount"] = 7
        self.assertFalse(self.assess(changed)[0])
        reference.write_bytes(bytes(32))
        self.assertFalse(self.assess()[0])

    def test_float_reference_does_not_allow_different_actual_digests_across_arms(self) -> None:
        self.use_float_reference()
        changed = deepcopy(self.sample)
        changed["traceMeta"]["timedOutputProof"]["observations"][0]["actualSha256"] = "f" * 64
        self.assertFalse(self.assess(changed)[0])

    def test_failed_execution_and_empty_measured_arm_are_rejected(self) -> None:
        changed = deepcopy(self.sample)
        changed["returnCode"] = 1
        self.assertFalse(self.assess(changed)[0])
        self.assertFalse(assess_plan_output_proof([], [self.sample])[0])


if __name__ == "__main__":
    unittest.main()
