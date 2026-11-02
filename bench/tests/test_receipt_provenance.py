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
