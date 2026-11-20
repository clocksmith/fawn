import importlib.util
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "resident_comparison", Path(__file__).resolve().parents[1] / "resident-comparison.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ResidentComparisonTest(unittest.TestCase):
    def test_runner_gates_real_child_processes_without_a_browser(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            script = root / "phase.mjs"
            script.write_text("""
                import { writeFileSync,existsSync } from 'node:fs';
                import { setTimeout } from 'node:timers/promises';
                const env=process.env, start=performance.timeOrigin;
                writeFileSync(env.BLITZEN_RESIDENT_PROGRESS_FILE,JSON.stringify({phase:'ready',nodeTimeOriginMs:start}));
                while (!existsSync(env.BLITZEN_RESIDENT_BARRIER_FILE)) await setTimeout(10);
                const began=performance.now();
                await setTimeout(350);
                writeFileSync(env.BLITZEN_RESIDENT_RECEIPT_FILE,JSON.stringify({ verified:true,imageCorrect:true,
                  knownEffects:1,committedRevision:'B-r2',requestMonotonicMs:began,mutationReturnedMonotonicMs:began+1,
                  outcomeVerifiedMonotonicMs:performance.now(), plannerCalls:0, modelSpendUsd:0 }));
            """)
            args = SimpleNamespace(stock_channel="chrome", timeout=10)
            wave = {"width": 2, "repeat": 1, "arm": "stock-helper"}
            with patch.object(module, "FIXTURE", script):
                module.run_wave(root, wave, args)
            for slot in (1, 2):
                row = module.summarize_row(root / module.run_id(wave, slot), wave, slot)
                self.assertEqual(row["status"], "verified")
                self.assertGreater(row["peakSampledTreeRssBytes"], 0)
                self.assertGreaterEqual(row["pageChangeToVerifiedBoundsMs"][0], 300)
            self.assertTrue((root / (module.run_id(wave, 0) + ".go")).exists())

    def test_incorrect_effect_and_failed_oracle_cannot_enter_latency(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            wave = {"width": 1, "repeat": 1, "arm": "stock-helper"}
            module.write(root / "exit.json", {"exit": 0, "allReady": True})
            module.write(
                root / "outcome.json",
                {"verified": True, "imageCorrect": True, "knownEffects": 2, "committedRevision": "B-r2"},
            )
            row = module.summarize_row(root, wave, 1)
            self.assertEqual(row["status"], "incorrect")
            self.assertIsNone(row["pageChangeToVerifiedMidpointMs"])
            module.write(root / "exit.json", {"exit": 1, "allReady": True})
            module.write(
                root / "progress.json", {"failureType": "AssertionError", "failure": "wrong tile"}
            )
            self.assertEqual(module.summarize_row(root, wave, 1)["reason"], "wrong tile")

    def test_probe_overhead_is_never_a_latency_result(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module.write(root / "exit.json", {"exit": 0, "allReady": True})
            module.write(
                root / "outcome.json",
                {
                    "verified": True,
                    "imageCorrect": True,
                    "knownEffects": 1,
                    "committedRevision": "B-r2",
                    "diagnosticOnly": True,
                },
            )
            row = module.summarize_row(
                root, {"arm": "stock-dawn", "width": 1, "repeat": 1}, 1
            )
            self.assertEqual(row["status"], "unqualified")
            self.assertIsNone(row["pageChangeToVerifiedMidpointMs"])

    def test_missing_refusals_source_and_page_ownership(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            plan = module.make_plan(2, [1, 2])
            module.write(root / "plan.json", plan)
            for wave in plan:
                for slot in range(1, wave["width"] + 1):
                    d = root / module.run_id(wave, slot)
                    if (wave["arm"], wave["repeat"], slot) == ("stock-helper", 2, 1):
                        continue
                    d.mkdir()
                    gpu = {"vendor": "intel", "isFallbackAdapter": False}
                    revision = (
                        "different" if wave["arm"] == "fawn-dawn" and wave["repeat"] == 2 else "shared"
                    )
                    module.write(
                        d / "progress.json",
                        {
                            "phase": "ready",
                            "nodeTimeOriginMs": 1000000,
                            "gpu": gpu,
                            "browser": {"revision": revision, "product": "Chromium/140.1"},
                            "targetId": "actual-tab",
                            "surfaceId": "actual-surface",
                            "partial": {"plannerCalls": 0, "modelSpendUsd": 0},
                            "claimedDoeProof": True,
                            "selectedRuntime": "doe",
                            "fallbackApplied": False,
                        },
                    )
                    module.write(d / "exit.json", {"exit": 0, "allReady": True})
                    first = 1000 if wave["arm"] == "stock-helper" and slot == 2 else 1
                    module.write(
                        d / "outcome.json",
                        {
                            "imageCorrect": True,
                            "verified": True,
                            "knownEffects": 1,
                            "committedRevision": "B-r2",
                            "requestMonotonicMs": first,
                            "mutationReturnedMonotonicMs": first + 2,
                            "outcomeVerifiedMonotonicMs": first + 12 + slot,
                            "plannerCalls": 0,
                            "modelSpendUsd": 0,
                        },
                    )
            refused = root / module.run_id({"width": 1, "repeat": 2, "arm": "fawn-doe"}, 1)
            (refused / "outcome.json").unlink()
            module.write(refused / "exit.json", {"exit": 1, "allReady": False})
            report = module.summarize(root)
            rows = report["rows"]
            self.assertEqual(len(rows), 24)
            self.assertEqual(sum(r["status"] == "missing" for r in rows), 2)
            self.assertEqual(sum(r["status"] == "unattributed" for r in rows), 5)
            self.assertTrue(
                all(
                    r["pageChangeToVerifiedMidpointMs"] is None and r["requestMonotonicMs"] is None
                    for r in rows
                    if r["arm"] == "fawn-doe"
                )
            )
            row = next(r for r in rows if r["status"] == "refused")
            self.assertEqual(row["modelSpendUsd"], 0)
            self.assertIsNone(row["cdpPixelPayloadBytes"])
            row = next(
                r
                for r in report["paired"]
                if r["baseline"] == "stock-dawn"
                and r["candidate"] == "fawn-dawn"
                and r["concurrency"] == 1
            )
            self.assertEqual(
                (row["n"], row["p95Ms"], row["omitted"]), (1, 0, {"browser source/version unmatched": 1})
            )
            row = next(
                r for r in report["byArm"] if r["arm"] == "stock-helper" and r["concurrency"] == 2
            )
            self.assertEqual(row["n"], 0)
            self.assertEqual(module.stats(range(1, 21))["p95Ms"], 19)
            self.assertFalse(
                module.hardware_adapter(
                    {"vendor": "Google", "architecture": "SwiftShader", "isFallbackAdapter": False}
                )
            )
            self.assertFalse(module.hardware_adapter({"vendor": "apple", "isFallbackAdapter": None}))


if __name__ == "__main__":
    unittest.main()
