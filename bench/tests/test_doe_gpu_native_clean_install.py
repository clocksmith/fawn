from __future__ import annotations

import hashlib
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
REPORTS = {
    runtime: (
        ROOT
        / "reports/benchmarks/amd-vulkan/20260816T-current-runtime-package"
        / f"doe-gpu-{runtime}-native-clean-install-diagnostic.json"
    )
    for runtime in ("node", "bun", "electron")
}
RELIABILITY_REPORTS = {
    runtime: (
        ROOT
        / "reports/benchmarks/amd-vulkan/20260828T152721Z"
        / f"doe-gpu-{runtime}-native-release-candidate.reliability.json"
    )
    for runtime in ("node", "bun", "electron")
}
AUGUST_CANDIDATES = {
    runtime: (
        ROOT
        / "reports/benchmarks/amd-vulkan/20260828T152721Z"
        / f"doe-gpu-{runtime}-native-release-candidate.json"
    )
    for runtime in ("node", "bun", "electron")
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class DoeGpuNativeCleanInstallTests(unittest.TestCase):
    def test_reviewed_artifact_preserves_package_claim_boundary(self) -> None:
        for runtime, path in REPORTS.items():
            with self.subTest(runtime=runtime):
                report = json.loads(path.read_text())
                self.assertEqual(report["status"], "passed")
                self.assertEqual(
                    report["tuple"], {"platform": "linux", "arch": "x64"}
                )
                self.assertEqual(report["runtime"]["host"], runtime)
                self.assertEqual(
                    report["decision"]["nativePackageCleanInstall"],
                    "authorized-for-declared-runtime-tuple",
                )
                self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
                self.assertFalse(report["decision"]["performanceCredit"])
                self.assertFalse(
                    report["decision"]["applicationPromotionCredit"]
                )
                self.assertFalse(
                    report["installation"]["workspaceLibraryResolution"]
                )
                if runtime == "electron":
                    self.assertEqual(report["runtime"]["version"], "43.4.0")
                    self.assertEqual(
                        report["launch"]["mode"],
                        "electron-main-process-node-side",
                    )
                    self.assertFalse(report["launch"]["rendererCreated"])

    def test_august_candidates_bind_their_retained_reliability_reports(self) -> None:
        for runtime, path in AUGUST_CANDIDATES.items():
            with self.subTest(runtime=runtime):
                candidate = json.loads(path.read_text())
                reliability_path = RELIABILITY_REPORTS[runtime]
                reliability = json.loads(reliability_path.read_text())
                reference = candidate["reliabilityEvidence"]
                self.assertEqual(reference["path"], reliability_path.relative_to(ROOT).as_posix())
                self.assertEqual(reference["sha256"], sha256(reliability_path))
                self.assertEqual(reference["artifactKind"], reliability["artifactKind"])
                self.assertEqual(reference["status"], reliability["status"])
                self.assertEqual(candidate["packages"], reliability["packages"])
                for field in ("firstKernel", "wrapperManifest", "platformManifest"):
                    self.assertEqual(candidate["implementation"][field], reliability["implementation"][field])

    def test_installed_native_runtime_and_oracle_are_exact(self) -> None:
        for runtime, path in REPORTS.items():
            with self.subTest(runtime=runtime):
                report = json.loads(path.read_text())
                provider = report["receipt"]["provider"]
                self.assertEqual(report["receipt"]["runtimeHost"], runtime)
                if runtime == "electron":
                    self.assertEqual(
                        report["receipt"]["runtimeVersion"],
                        report["runtime"]["version"],
                    )
                    self.assertEqual(
                        report["receipt"]["mappedRangeProbe"],
                        {
                            "objectTag": "[object ArrayBuffer]",
                            "sliceAvailable": True,
                            "value": 42,
                        },
                    )
                self.assertTrue(provider["loaded"])
                self.assertTrue(provider["doeNative"])
                self.assertEqual(provider["buildMetadataSource"], "prebuild")
                self.assertIn(
                    "/node_modules/doe-gpu-linux-x64/",
                    provider["doeLibraryPath"],
                )
                self.assertEqual(
                    report["receipt"]["result"]["output"],
                    [2, 4, 6, 8, 10, 12, 14, 16],
                )
                self.assertEqual(
                    report["receipt"]["result"]["outputSha256"],
                    "9d42cad41af4aaf3ae973e5a48d96f61e4708edc57bf7dd29a497ebd96f506cf",
                )

    def test_bounded_reliability_receipts_cover_fresh_and_concurrent_processes(
        self,
    ) -> None:
        for runtime, path in RELIABILITY_REPORTS.items():
            with self.subTest(runtime=runtime):
                report = json.loads(path.read_text())
                self.assertEqual(report["status"], "passed")
                self.assertEqual(
                    report["tuple"],
                    {"runtime": runtime, "platform": "linux", "arch": "x64"},
                )
                self.assertEqual(report["contract"]["sequentialTrials"], 3)
                self.assertEqual(report["contract"]["concurrentTrials"], 2)
                self.assertEqual(len(report["trials"]), 5)
                self.assertEqual(report["contract"]["lifecycleCycles"], 12)
                self.assertEqual(report["contract"]["lifecycleWarmupCycles"], 2)
                self.assertEqual(
                    [trial["mode"] for trial in report["trials"]].count(
                        "sequential"
                    ),
                    3,
                )
                self.assertEqual(
                    [trial["mode"] for trial in report["trials"]].count(
                        "concurrent"
                    ),
                    2,
                )
                for trial in report["trials"]:
                    self.assertEqual(trial["exitCode"], 0)
                    self.assertIsNone(trial["signal"])
                    self.assertFalse(trial["timedOut"])
                    self.assertFalse(trial["outputLimitExceeded"])
                    self.assertEqual(
                        trial["receipt"]["outputSha256"],
                        "9d42cad41af4aaf3ae973e5a48d96f61e4708edc57bf7dd29a497ebd96f506cf",
                    )
                self.assertEqual(
                    report["decision"]["boundedCleanProcessReliability"],
                    "authorized-for-declared-runtime-tuple",
                )
                self.assertEqual(
                    report["decision"]["boundedSameProcessLifecycle"],
                    "authorized-for-declared-runtime-tuple",
                )
                self.assertEqual(
                    report["decision"]["boundedRssGrowthDiagnostic"],
                    "authorized-for-declared-runtime-tuple",
                )
                lifecycle = report["sameProcessLifecycle"]
                self.assertEqual(lifecycle["exitCode"], 0)
                self.assertIsNone(lifecycle["signal"])
                self.assertFalse(lifecycle["timedOut"])
                self.assertFalse(lifecycle["outputLimitExceeded"])
                self.assertEqual(lifecycle["cycleCount"], 12)
                self.assertEqual(lifecycle["warmupCycles"], 2)
                self.assertLessEqual(
                    lifecycle["postWarmupRssSpanBytes"],
                    lifecycle["maxPostWarmupRssSpanBytes"],
                )
                self.assertEqual(len(lifecycle["samples"]), 12)
                for sample in lifecycle["samples"]:
                    self.assertTrue(sample["deviceDestroyed"])
                    self.assertEqual(sample["lostReason"], "destroyed")
                    self.assertTrue(sample["postDestroyRejected"])
                    self.assertIn(
                        "GPUDevice was destroyed", sample["postDestroyError"]
                    )
                    self.assertEqual(
                        sample["outputSha256"],
                        "9d42cad41af4aaf3ae973e5a48d96f61e4708edc57bf7dd29a497ebd96f506cf",
                    )
                self.assertFalse(report["decision"]["deviceLossCredit"])
                self.assertEqual(
                    report["decision"]["deliberateDestroyLossSemantics"],
                    "authorized-for-declared-runtime-tuple",
                )
                self.assertFalse(report["decision"]["memoryGrowthCredit"])
                self.assertFalse(report["decision"]["performanceCredit"])
                self.assertFalse(
                    report["decision"]["applicationPromotionCredit"]
                )
                if runtime == "electron":
                    self.assertEqual(
                        report["launch"]["mode"],
                        "electron-main-process-node-side",
                    )
                    self.assertFalse(report["launch"]["rendererCreated"])


if __name__ == "__main__":
    unittest.main()
