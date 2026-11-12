"""Governed compute plans carry CPU command-graph oracles the plan proof admits."""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import unittest

from bench.native_compare_modules.plan_output_proof import assess_plan_output_proof
from bench.tools import generate_governed_plan_ir as governed


REPO_ROOT = Path(__file__).resolve().parents[2]
CATALOGS = ("bench/workloads/workloads.amd.vulkan.json", "bench/workloads/workloads.apple.metal.json")


def _governed_plan_rows() -> list[dict]:
    rows = []
    for catalog in CATALOGS:
        for row in json.loads((REPO_ROOT / catalog).read_text())["workloads"]:
            if "governed" in row.get("cohorts", []) and row.get("irPath") == "bench/ir/compute_governed.json":
                rows.append(row)
    return rows


def _proof_sample(plan_path: Path, actual_sha256: str) -> dict:
    plan = json.loads(plan_path.read_text())
    seq, command = next((i, c) for i, c in enumerate(plan["commands"]) if "output_oracle" in c)
    oracle = command["output_oracle"]
    target = next(b for b in command["bindings"] if b["binding"] == oracle["binding"])
    return {"returnCode": 0, "success": True, "traceMeta": {
        "hostPlanArtifactPath": str(plan_path), "hostPlanArtifactHash": plan["planSha256"],
        "outputOracleCount": 1, "outputOracleMatchedCount": 1, "outputOracleFailedCount": 0,
        "executionErrorCount": 0, "executionUnsupportedCount": 0, "executionSkippedCount": 0,
        "timedOutputProof": {
            "schema": "doe.plan-output-proof/v1", "executionScope": "same-execution",
            "planSha256": plan["planSha256"], "planFileSha256": hashlib.sha256(plan_path.read_bytes()).hexdigest(),
            "observations": [{
                "commandSeq": seq, "kernel": command["kernel"], "entryPoint": "main", "bindingGroup": 0,
                "binding": oracle["binding"], "resourceHandle": target["resource_handle"],
                "byteLength": target["buffer_size"], "dispatchCount": 1, "actualSha256": actual_sha256,
                "matched": True, "comparedValueCount": 0, "mismatchCount": 0,
                "oracle": {"kind": "sha256_exact_v1", "referenceClass": "independent_v1",
                           "expectedSha256": oracle["expected_sha256"], "referenceId": oracle["reference_id"],
                           "referencePath": None, "absoluteTolerance": 0, "relativeTolerance": 0}}],
        },
    }}


class GovernedPlanIrTests(unittest.TestCase):
    def test_ir_matches_its_cpu_references(self) -> None:
        self.assertEqual(governed.IR_PATH.read_text(encoding="utf-8"), governed.render(governed.build_ir()))

    def test_governed_compute_rows_run_single_dispatch_plans_with_graph_oracles(self) -> None:
        rows = _governed_plan_rows()
        amd = {r["id"] for r in rows if r["vendor"] == "amd"}
        apple = {r["id"] for r in rows if r["vendor"] == "apple"}
        self.assertEqual(len(amd), 7)
        self.assertEqual(len(apple), 5)
        for row in rows:
            plan = json.loads((REPO_ROOT / row["planPath"]).read_text())
            self.assertEqual(plan["planSha256"], row["planHash"])
            dispatches = [c for c in plan["commands"] if c["kind"] == "kernel_dispatch"]
            self.assertTrue(all("repeat" not in c for c in dispatches), row["id"])
            oracles = [c["output_oracle"] for c in plan["commands"] if "output_oracle" in c]
            self.assertEqual(len(oracles), 1, row["id"])
            self.assertIn("output_oracle", plan["commands"][-1])
            self.assertEqual((oracles[0]["schema_version"], oracles[0]["scope"], oracles[0]["reference_class"],
                              oracles[0]["dispatch_count"]), (2, "command_graph", "independent_v1", 1))

    def test_recurrence_oracle_covers_every_unrolled_dispatch(self) -> None:
        plan = json.loads((REPO_ROOT / "bench/plans/generated/compute_workgroup_atomic_1024.plan.json").read_text())
        self.assertEqual(plan["dispatchCount"], 100)
        expected = hashlib.sha256(governed.recurrence.expected_output(dispatch_count=100)).hexdigest()
        self.assertEqual(plan["commands"][-1]["output_oracle"]["expected_sha256"], expected)
        self.assertNotEqual(expected, hashlib.sha256(governed.recurrence.expected_output(dispatch_count=99)).hexdigest())

    def test_plan_proof_admits_matching_output_and_rejects_a_mismatch(self) -> None:
        for row in _governed_plan_rows():
            path = REPO_ROOT / row["planPath"]
            expected = json.loads(path.read_text())["commands"][-1]["output_oracle"]["expected_sha256"]
            good = _proof_sample(path, expected)
            admitted, details = assess_plan_output_proof([good], [deepcopy(good)])
            self.assertTrue(admitted, (row["id"], details))
            wrong = _proof_sample(path, "0" * 64)
            self.assertFalse(assess_plan_output_proof([good], [wrong])[0], row["id"])


if __name__ == "__main__":
    unittest.main()
