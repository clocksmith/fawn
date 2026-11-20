import importlib.util
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import resident_blocks as blocks

spec = importlib.util.spec_from_file_location("physical_runner", ROOT / "resident-comparison.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class Controls(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.out = Path(self.tmp.name)
        self.plan = runner.make_plan(2, [1, 2], sparse_controls=True)
        runner.write(self.out / "plan.json", self.plan)
        receipt = {"chromeSha256": "c" * 64, "chromium": "a" * 40, "dawn": "b" * 40}
        self.contract = {
            "planSha256": blocks.digest(self.out / "plan.json"),
            "hostBootSha256": "1" * 64,
            "expectedDrmDevice": "gpu@0000:02:00.0",
            "sourceReceipts": {"stock": receipt, "fawn": receipt},
            "planner": "fixed compiled rule",
        }
        self.args = SimpleNamespace(matched_blocks=1)
        self.orig = SimpleNamespace(
            make_plan=runner.make_plan,
            run_id=runner.run_id,
            read=runner.read,
            CPU_ARMS=runner.CPU_ARMS,
            hardware_adapter=runner.hardware_adapter,
            run_wave=self.wave,
            oracle_report=lambda *_: {"verified": True},
        )
        self.executed = []
        self.stop = None
        self.device = self.contract["expectedDrmDevice"]
        self.gpu = {
            "vendor": "amd",
            "device": "gpu-fixture",
            "architecture": "synthetic-test",
            "isFallbackAdapter": False,
        }

    def wave(self, path, wave, args):
        self.executed.append(wave)
        for n in range(1, wave["width"] + 1):
            row = path / runner.run_id(wave, n)
            row.mkdir()
            runner.write(
                row / "progress.json",
                {
                    "selection": {"test": True},
                    "gpu": self.gpu,
                    "partial": {"plannerCalls": 2, "modelSpendUsd": 0.03},
                },
            )
            if wave == self.stop:
                raise InterruptedError("synthetic interruption at predeclared row")
            runner.write(
                row / "exit.json",
                {"allReady": True, "exit": 0, "gpuProcessDrm": {"devicesSeen": [self.device]}},
            )
            runner.write(
                row / "outcome.json", {"selection": {"test": True}, "plannerCalls": 0, "modelSpendUsd": 0}
            )
            runner.write(
                row / "browser-source.json",
                {
                    **self.contract["sourceReceipts"]["fawn"],
                    "hostBootSha256": self.contract["hostBootSha256"],
                },
            )

    def start(self, identity=None):
        return blocks.run(self.out, self.plan, self.args, self.orig, identity or self.contract)

    def test_block_boundaries_preserve_counterbalance_and_visibility(self):
        self.assertEqual(self.start(), [0])
        self.assertEqual(len(self.executed), 2)
        self.assertTrue((self.out / runner.run_id(self.plan[0], 1) / "exit.json").is_file())
        self.assertFalse((self.out / runner.run_id(self.plan[2], 1) / "exit.json").is_file())
        self.assertEqual(self.start(), [1])
        self.assertEqual(len(self.executed), 12)
        self.args.matched_blocks = 3
        self.assertEqual(self.start(), [2, 3, 4])
        self.assertEqual(self.executed, self.plan)
        self.assertEqual(self.start(), [])

    def test_failures_preserve_observed_spend_and_skip_whole_block(self):
        self.start()
        self.stop = self.plan[5]
        with self.assertRaises(InterruptedError):
            self.start()
        self.assertFalse((self.out / runner.run_id(self.plan[2], 1)).exists())
        info = blocks.audit(self.out, self.plan, self.orig)["blocks"][1]
        self.assertEqual((info["state"], info["plannedSlots"]), ("incomplete", 10))
        calls = info["excludedAttempts"]
        self.assertEqual(len(calls), 4)
        self.assertEqual((calls[-1]["plannerCalls"], calls[-1]["modelSpendUsd"]), (2, 0.03))
        self.stop = None
        self.assertEqual(self.start(), [2])
        self.assertEqual(self.executed[:6], self.plan[:6])
        self.assertEqual(self.executed[6], self.plan[12])

    def test_substitution_is_refused_before_gpu(self):
        self.start()
        n = len(self.executed)
        for key, value in (("hostBootSha256", "other"), ("expectedDrmDevice", "new"), ("planner", "new")):
            with self.assertRaisesRegex(ValueError, "frozen"):
                self.start({**self.contract, key: value})
        self.assertEqual(len(self.executed), n)
        row = self.out / runner.run_id(self.plan[0], 1) / "outcome.json"
        runner.write(row, {"tampered": True})
        with self.assertRaisesRegex(ValueError, "accepted block content changed"):
            self.start()
        self.assertEqual(len(self.executed), n)

    def test_observed_wrong_card_and_orphan_refusal(self):
        self.start()
        self.device = "other@0000:05:00.0"
        with self.assertRaisesRegex(ValueError, "DRM"):
            self.start()
        self.assertFalse((self.out / runner.run_id(self.plan[2], 1)).exists())
        (self.out / "block-ledger/pending/002").mkdir()
        with self.assertRaisesRegex(ValueError, "partial block 002"):
            self.start()

    def test_failed_oracles_cannot_seed_a_new_window(self):
        self.orig.oracle_report = lambda *_: {"verified": False}
        with self.assertRaisesRegex(ValueError, "control refused"):
            self.start()
        with self.assertRaisesRegex(ValueError, "interrupted correctness"):
            self.start()
        self.assertFalse((self.out / runner.run_id(self.plan[0], 1)).exists())

    def test_report_never_counts_tampered_or_partial_data(self):
        self.orig.summarize = lambda out: {"stats": "unchanged original reporter"}
        self.start()
        self.stop = self.plan[3]
        with self.assertRaises(InterruptedError):
            self.start()
        blocks.report(self.out, self.orig)
        good = runner.read(self.out / "report.json")
        self.assertEqual(good["stats"], "unchanged original reporter")
        self.assertEqual(good["blockLedger"]["blocks"][1]["state"], "incomplete")
        runner.write(self.out / runner.run_id(self.plan[0], 1) / "outcome.json", {"changed": True})
        with self.assertRaisesRegex(ValueError, "content changed"):
            blocks.report(self.out, self.orig)
        self.assertFalse(runner.read(self.out / "report.json")["timingQualified"])

    def test_webgpu_substitution_on_same_drm_device(self):
        self.start()
        self.gpu["architecture"] = "different"
        with self.assertRaisesRegex(ValueError, "WebGPU adapter differs"):
            self.start()
        self.assertFalse((self.out / runner.run_id(self.plan[2], 1)).exists())

    def test_changed_plan_order(self):
        self.plan[2], self.plan[3] = self.plan[3], self.plan[2]
        with self.assertRaisesRegex(ValueError, "ordered"):
            self.start()


if __name__ == "__main__":
    unittest.main()
