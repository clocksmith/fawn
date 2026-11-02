"""New receipts observe host facts and exact artifact bytes without guessing sources."""

from __future__ import annotations

import argparse
from contextlib import redirect_stdout
from copy import deepcopy
import io
from unittest.mock import patch
import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from bench.inventory import model
from bench.lib import receipt_provenance as provenance
from bench.tests.test_benchmark_inventory import observation


class TestReceiptProvenance(unittest.TestCase):
    def test_execution_unknowns_replace_supplemental_source_assertions(self) -> None:
        row = observation("supplemental", None)
        row["implementation"].update(sourceCommit="a" * 40, binaryHash="old-binary",
                                     componentHashes={"old-compiler": "old-hash"})
        envelope = provenance.capture_provenance(executed_at="2026-11-02T12:00:00Z",
                                                implementation={"name": "doe"})
        model.apply_provenance(row, {"provenance": envelope})
        self.assertIsNone(row["implementation"]["sourceCommit"])
        self.assertIsNone(row["implementation"]["binaryHash"])
        self.assertEqual(row["implementation"]["componentHashes"], {})
        self.assertFalse(model.inventory_qualified(row))

    def test_comparison_uses_execution_clocks_and_rejects_mixed_artifacts(self) -> None:
        row = observation("comparison", None)
        envelope = provenance.capture_provenance(executed_at="2026-11-02T12:00:00Z",
                                                implementation={"name": "doe"})
        records = [{"provenance": deepcopy(envelope), "executionTimeSource": "raw#1"},
                   {"provenance": deepcopy(envelope), "executionTimeSource": "raw#2"}]
        records[1]["provenance"]["executedAt"] = "2026-11-02T12:01:00Z"
        records[0]["provenance"]["implementation"]["binaryHash"] = "a" * 64
        records[1]["provenance"]["implementation"]["binaryHash"] = "b" * 64
        model.apply_comparison_provenance(row, {"implementation": records, "comparator": [records[0]]})
        self.assertEqual(row["executedAt"], "2026-11-02T12:01:00Z")
        self.assertEqual(row["executionTimeSource"], "raw#2")
        self.assertIsNone(row["implementation"]["binaryHash"])
        self.assertTrue(any("different artifact identities" in value for value in row["limitations"]))

    def test_existing_native_package_sdk_and_dawn_build_receipts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library = root / "provider"
            library.write_bytes(b"actual provider")
            digest = hashlib.sha256(library.read_bytes()).hexdigest()
            receipts = [
                {"source_commit": "a" * 40, "artifacts": {"lib/provider": digest}},
                {"commit": "b" * 40, "binary_sha256": {"provider.dll": digest}},
                {"source": "https://dawn.googlesource.com/dawn", "revision": "c" * 40,
                 "products_sha256": {"webgpu_dawn.dll": digest}},
            ]
            receipt = root / "build.json"
            for i, binding in enumerate(receipts):
                receipt.write_text(json.dumps(binding))
                actual, _ = provenance.artifact_identity({"binaryPath": str(library),
                                                          "buildReceiptPath": str(receipt)})
                self.assertEqual(actual["sourceCommit"], "abc"[i] * 40)
            frozen = provenance.file_hash(receipt)
            receipt.write_text(json.dumps({"sourceCommit": "d" * 40, "binaryHash": digest}))
            actual, _ = provenance.artifact_identity({"binaryPath": str(library),
                "buildReceiptPath": str(receipt), "expectedBuildReceiptHash": frozen})
            self.assertIsNone(actual["sourceCommit"])

    def test_replaced_compiler_cannot_inherit_provider_source_binding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            library, compiler, receipt = root / "provider", root / "compiler", root / "build.json"
            library.write_bytes(b"provider")
            compiler.write_bytes(b"original compiler")
            old_compiler = provenance.file_hash(compiler)
            receipt.write_text(json.dumps({"sourceCommit": "a" * 40,
                                          "binaryHash": provenance.file_hash(library)}))
            compiler.write_bytes(b"replacement compiler")
            actual, limitations = provenance.artifact_identity({"binaryPath": str(library),
                "componentPaths": {"compiler": str(compiler)}, "expectedComponentHashes": {"compiler": old_compiler},
                "buildReceiptPath": str(receipt)})
            self.assertIsNone(actual["sourceCommit"])
            self.assertNotIn("compiler", actual["componentHashes"])
            self.assertTrue(any("Component compiler changed" in value for value in limitations))

    def test_binary_requires_matching_source_receipt_and_stable_bytes(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "runtime"
            binary.write_bytes(b"actual artifact")
            digest = hashlib.sha256(b"actual artifact").hexdigest()
            spec = {"name": "doe", "binaryPath": str(binary)}
            actual, _ = provenance.artifact_identity(spec)
            self.assertEqual(actual["binaryHash"], digest)
            self.assertIsNone(actual["sourceCommit"])
            receipt = root / "build.json"
            receipt.write_text(
                json.dumps({"sourceCommit": "a" * 40, "binaryHash": digest})
            )
            spec["buildReceiptPath"] = str(receipt)
            actual, _ = provenance.artifact_identity(spec)
            self.assertEqual(actual["sourceCommit"], "a" * 40)
            binary.write_bytes(b"replacement")
            spec["expectedBinaryHash"] = digest
            actual, reasons = provenance.artifact_identity(spec)
            self.assertIsNone(actual["binaryHash"])
            self.assertIsNone(actual["sourceCommit"])
            self.assertTrue(
                any("changed during" in reason for reason in reasons)
            )

    def test_metadata_binding_detects_replacement_without_prewarming_binary(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "binary"
            path.write_bytes(b"original")
            stat = provenance.artifact_stat(path)
            unchanged, _ = provenance.artifact_identity(
                {"name": "doe", "binaryPath": str(path), "expectedStat": stat}
            )
            self.assertEqual(
                unchanged["binaryHash"], hashlib.sha256(b"original").hexdigest()
            )
            path.write_bytes(b"replacement")
            changed, reasons = provenance.artifact_identity(
                {"name": "doe", "binaryPath": str(path), "expectedStat": stat}
            )
            self.assertIsNone(changed["binaryHash"])
            self.assertTrue(
                any("changed during" in reason for reason in reasons)
            )

    def test_envelope_preserves_unknown_adapter_and_historical_receipts(
        self,
    ) -> None:
        value = provenance.capture_provenance(
            executed_at="2026-11-02T12:00:00Z", implementation={"name": "doe"}
        )
        provenance.validate_provenance(value)
        self.assertIsNone(value["machine"]["gpu"])
        self.assertIsNone(value["machine"]["driver"])
        row = observation("old", None, machine=None)
        before = deepcopy(row)
        model.apply_provenance(row, {"provenance": {"oldFormat": True}})
        self.assertEqual(row, before)
        model.apply_provenance(row, {"provenance": value})
        self.assertEqual(row["executedAt"], "2026-11-02T12:00:00Z")
        self.assertEqual(row["machine"]["id"], value["machine"]["id"])
        self.assertFalse(model.inventory_qualified(row))
        with self.assertRaises(ValueError):
            provenance.capture_provenance(
                executed_at="2026-11-02", implementation={}
            )

    def test_observed_runtime_adapter_cannot_be_replaced_by_host_defaults(
        self,
    ) -> None:
        value = provenance.capture_provenance(
            executed_at="2026-11-02T12:00:00Z",
            implementation={"name": "doe"},
            machine_observation={
                "id": "enrolled-host",
                "identitySource": "receipt:host-admission",
                "gpu": "actual-adapter",
                "driver": "actual-driver",
            },
        )
        provenance.validate_provenance(value)
        self.assertEqual(value["machine"]["id"], "enrolled-host")
        self.assertEqual(value["machine"]["gpu"], "actual-adapter")
        self.assertEqual(value["machine"]["driver"], "actual-driver")

    def test_failed_native_process_retains_timing_and_new_provenance(
        self,
    ) -> None:
        from bench.tools import dropin_phase_cohort as phase

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "harness"
            binary.write_bytes(b"fixture executable")
            out = root / "process.jsonl"
            args = argparse.Namespace(
                provider=["doe=" + str(binary)],
                out=str(out),
                workload=["fixture"],
                inputs=str(root),
                iterations=1,
                inflight_depth=1,
                pairs=1,
                timeout=1,
            )
            with (
                patch.object(phase, "harness_args", return_value=["--fixture"]),
                patch.object(
                    phase,
                    "run_process",
                    return_value=(1, '{"status":"failed"}', "error", 123, 7),
                ),
                redirect_stdout(io.StringIO()),
            ):
                with self.assertRaises(SystemExit):
                    phase.run(args)
            receipt = json.loads(out.read_text())
            self.assertEqual(receipt["returnCode"], 1)
            self.assertEqual(receipt["processWallNs"], 123)
            self.assertEqual(receipt["childCpuMs"], 7)
            provenance.validate_provenance(receipt["provenance"])
            self.assertEqual(
                receipt["provenance"]["implementation"]["binaryHash"],
                hashlib.sha256(b"fixture executable").hexdigest(),
            )
