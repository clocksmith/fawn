#!/usr/bin/env python3
"""Package executors check a direct plan's declared output oracles."""

from __future__ import annotations

import hashlib
import json
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PLAN_URL = (ROOT / "bench/executors/node-webgpu/plan.js").as_uri()


def _command_plan(oracle: dict) -> dict:
    dispatch = {
        "kind": "kernel_dispatch", "kernel": "workgroup_atomic.wgsl",
        "x": 1, "y": 1, "z": 1, "initialize_buffers_on_create": True,
        "bindings": [{"binding": 2, "group": 0, "kind": "buffer", "buffer_type": "storage",
                      "resource_handle": 4204, "buffer_size": 16, "visibility": "compute"}],
        "output_oracle": oracle,
    }
    return {"schemaVersion": 1, "workloadId": "oracle_probe", "planSha256": "cd" * 32,
            "commandCount": 1, "bufferWriteCount": 0, "dispatchCount": 1, "commands": [dispatch]}


def _tolerance_oracle(reference: Path) -> dict:
    return {
        "schema_version": 3, "scope": "command_graph", "reference_class": "independent_v1",
        "kind": "float32_reference_tolerance_v1", "initialization": "zero_fill_v1",
        "binding_group": 0, "binding": 2, "dispatch_count": 1,
        "reference_sha256": hashlib.sha256(reference.read_bytes()).hexdigest(),
        "reference_id": "independent-reference", "reference_path": str(reference),
        "absolute_tolerance": 1e-6, "relative_tolerance": 1e-5,
    }


def _run_node(script: str) -> dict:
    completed = subprocess.run(
        ["node", "--input-type=module", "-e",
         f"import * as plan from {json.dumps(PLAN_URL)};\n{script}"],
        capture_output=True, text=True, check=True, cwd=ROOT,
    )
    return json.loads(completed.stdout)


@unittest.skipIf(shutil.which("node") is None, "node is required")
class PackageOutputOracleTests(unittest.TestCase):
    def test_declared_oracle_becomes_a_checked_readback_of_its_binding(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            reference = Path(tmp) / "reference.f32le.bin"
            reference.write_bytes(struct.pack("<4f", 1.0, 2.0, 3.0, 4.0))
            plan = _command_plan(_tolerance_oracle(reference))
            result = _run_node(
                f"const p = plan.normalizePlan({json.dumps(plan)});\n"
                "process.stdout.write(JSON.stringify(p.steps.slice(-2)));"
            )
        copy, read = result
        self.assertEqual((copy["kind"], copy["srcBufferId"], copy["sizeBytes"]),
                         ("copyBufferToBuffer", "buffer_4204", 16))
        self.assertEqual(read["kind"], "readBuffer")
        self.assertEqual(read["bufferId"], copy["dstBufferId"])
        self.assertEqual(read["validate"]["kind"], "outputOracleV1")
        self.assertEqual(read["validate"]["byteLength"], 16)

    def test_tolerance_oracle_accepts_only_values_inside_the_frozen_tolerance(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            reference = Path(tmp) / "reference.f32le.bin"
            reference.write_bytes(struct.pack("<4f", 1.0, 2.0, 3.0, 4.0))
            plan = _command_plan(_tolerance_oracle(reference))
            cases = {
                "exact": [1.0, 2.0, 3.0, 4.0],
                "within": [1.0 + 5e-6, 2.0, 3.0, 4.0],
                "outside": [1.0, 2.0, 3.1, 4.0],
                "nan": [1.0, float("nan"), 3.0, 4.0],
            }
            payload = {name: list(struct.pack("<4f", *values)) for name, values in cases.items()}
            result = _run_node(
                f"const p = plan.normalizePlan({json.dumps(plan)});\n"
                "const expectation = p.steps.at(-1).validate;\n"
                f"const cases = {json.dumps(payload)};\n"
                "const out = {};\n"
                "for (const [name, bytes] of Object.entries(cases)) {\n"
                "  out[name] = plan.validateSampleExpectation(new Uint8Array(bytes), expectation).ok;\n"
                "}\n"
                "out.short = plan.validateSampleExpectation(new Uint8Array(12), expectation).ok;\n"
                "process.stdout.write(JSON.stringify(out));"
            )
        self.assertEqual(result, {"exact": True, "within": True, "outside": False,
                                  "nan": False, "short": False})

    def test_exact_oracle_compares_the_complete_digest(self) -> None:
        good = bytes(range(16))
        oracle = {
            "schema_version": 2, "scope": "command_graph", "reference_class": "independent_v1",
            "kind": "sha256_exact_v1", "initialization": "zero_fill_v1", "binding_group": 0,
            "binding": 2, "dispatch_count": 1, "expected_sha256": hashlib.sha256(good).hexdigest(),
            "reference_id": "exact-reference",
        }
        result = _run_node(
            f"const p = plan.normalizePlan({json.dumps(_command_plan(oracle))});\n"
            "const expectation = p.steps.at(-1).validate;\n"
            f"const good = new Uint8Array({json.dumps(list(good))});\n"
            "const bad = good.slice(); bad[15] ^= 1;\n"
            "process.stdout.write(JSON.stringify([\n"
            "  plan.validateSampleExpectation(good, expectation).ok,\n"
            "  plan.validateSampleExpectation(bad, expectation).ok]));"
        )
        self.assertEqual(result, [True, False])

    def test_isolated_or_repeated_oracles_are_refused(self) -> None:
        isolated = {
            "schema_version": 1, "kind": "sha256_exact_v1", "initialization": "zero_fill_v1",
            "binding_group": 0, "binding": 2, "dispatch_count": 1,
            "expected_sha256": "a" * 64, "reference_id": "isolated",
        }
        repeated = {**isolated, "schema_version": 2, "scope": "command_graph",
                    "reference_class": "independent_v1", "dispatch_count": 4}
        for oracle in (isolated, repeated):
            with self.subTest(oracle=oracle["schema_version"]):
                completed = subprocess.run(
                    ["node", "--input-type=module", "-e",
                     f"import * as plan from {json.dumps(PLAN_URL)};\n"
                     f"plan.normalizePlan({json.dumps(_command_plan(oracle))});"],
                    capture_output=True, text=True, cwd=ROOT,
                )
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("command_graph", completed.stderr)


if __name__ == "__main__":
    unittest.main()
