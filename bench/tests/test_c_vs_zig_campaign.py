"""Protect campaign completeness, provenance rejection and owned timeout cleanup."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from bench.tools import run_c_vs_zig_campaign as campaign


class CampaignTests(unittest.TestCase):
    def test_successful_dispatch_without_output_proof_is_not_compared(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            args = argparse.Namespace(backend="metal", catalog="unused",
                                      iterations=1, warmup=0, timeout=1)

            def run(command: list[str], *unused: object) -> dict:
                product = command[command.index("--product") + 1]
                folder = Path(command[command.index("--out") + 1])
                folder = folder / "run-artifacts" / product
                folder.mkdir(parents=True)
                receipt = {
                    "runtimeIdentity": {"runtimeImplementation":
                                        "c" if product == "doe-c" else "zig"},
                    "execution": {"success": True, "timedSampleCount": 1},
                    "samples": [{"success": True, "returnCode": 0,
                                 "traceMeta": {
                                     "fallbackUsed": False,
                                     "executionErrorCount": 0,
                                     "executionUnsupportedCount": 0,
                                     "executionSkippedCount": 0,
                                     "executionDispatchCount": 200000,
                                     "outputOracleCount": 0,
                                     "outputOracleFailedCount": 0,
                                 }}],
                }
                (folder / "sample.run.json").write_text(json.dumps(receipt))
                return {"exitCode": 0, "timedOut": False}

            with patch.object(campaign, "execute", side_effect=run) as execute:
                result = campaign.row_result({"id": "render"}, output, args, 0)
            self.assertEqual(execute.call_count, 2)
            self.assertEqual(result["status"], "diagnostic")
            self.assertFalse(result["claimEligible"])
            self.assertFalse(result["comparisons"])
            self.assertTrue(result["admissionFailures"])
            self.assertTrue(all(p.get("receipt")
                                for p in result["providers"].values()))

    def test_zero_or_partial_oracle_counts_never_satisfy_admission(self) -> None:
        for count, matched in ((0, 0), (2, 1), (True, True)):
            receipt = {"samples": [{"traceMeta": {
                "outputOracleCount": count,
                "outputOracleMatchedCount": matched,
                "outputOracleFailedCount": 0,
                "outputOracleReferenceClass": "independent_v1",
            }}]}
            with self.subTest(count=count, matched=matched):
                self.assertTrue(campaign.oracle_failures(receipt))

    def test_gpu_timeout_does_not_start_the_other_provider(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            args = argparse.Namespace(backend="metal", catalog="unused", iterations=16, warmup=1, timeout=1)
            with patch.object(campaign, "execute", return_value={"exitCode": -9, "timedOut": True}) as run:
                result = campaign.row_result({"id": "blocked"}, Path(temporary), args, 0)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(result["status"], "gpu-completion-unknown")

    def test_failed_row_does_not_hide_later_workload(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ["config/benchmark-methodology-thresholds.json", "bench/cli.py",
                         "runtime/c/runtime-cli/bin/doe-c-runtime" + (".exe" if os.name == "nt" else ""),
                         "runtime/zig/zig-out/bin/doe-zig-runtime" + (".exe" if os.name == "nt" else ""), "provider.so"]:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("input", encoding="utf-8")
            (root / "catalog.json").write_text(json.dumps({"workloads": [{"id": "first"}, {"id": "second"}]}))
            args = argparse.Namespace(out=str(root / "out"), catalog="catalog.json", workload_id=[], backend="metal",
                                      iterations=16, warmup=1, timeout=10, input_artifact=[str(root / "provider.so")])
            with patch.object(campaign, "ROOT", root), patch.object(campaign.platform, "platform", return_value="fixture"), \
                    patch.object(campaign.subprocess, "check_output", return_value="ref\n"), \
                    patch.object(campaign, "row_result", side_effect=[RuntimeError("first failure"), {"id": "second", "status": "executed"}]) as run:
                self.assertEqual(campaign.campaign(args), 1)
            self.assertEqual(run.call_count, 2)
            ledger = json.loads((root / "out/ledger.json").read_bytes())
            self.assertEqual(ledger["plannedRuntimeRows"], ["first", "second"])
            self.assertEqual(ledger["failedRows"], ["first"])
            self.assertEqual(ledger["rows"][0]["failures"], ["first failure"])

    def test_rejects_wrong_runtime_and_unreported_fallback(self) -> None:
        receipt = {"runtimeIdentity": {"runtimeImplementation": "zig"},
                   "execution": {"success": True, "timedSampleCount": 1},
                   "samples": [{"success": True, "returnCode": 0, "traceMeta": {}}]}
        errors = campaign.receipt_failures(receipt, "c", 1)
        self.assertIn("unexpected runtime implementation", errors)
        self.assertIn("fallback state absent or enabled", errors)
        self.assertIn("executionUnsupportedCount absent or nonzero", errors)

    def test_timeout_retains_first_output_and_stops_owned_process(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "timeout.log"
            result = campaign.execute(
                [sys.executable, "-c", "import time; print('before timeout',flush=True); time.sleep(30)"],
                log, 0.5, os.environ.copy(),
            )
            self.assertTrue(result["timedOut"])
            self.assertNotEqual(result["exitCode"], 0)
            self.assertIn("before timeout", log.read_text())


if __name__ == "__main__":
    unittest.main()
