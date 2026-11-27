from __future__ import annotations

import hashlib
import json
import os
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "bench/external-projects/holoscript-snn-webgpu"
REPORT = (
    ROOT
    / "reports/benchmarks/amd-vulkan/20260815T193900Z"
    / "holoscript-doeproof-loader-diagnostic.json"
)
CLI_REPORT = (
    ROOT
    / "reports/benchmarks/amd-vulkan/20260815T200358Z"
    / "holoscript-doeproof-cli-diagnostic.json"
)
FILESYSTEM_REPORT = (
    ROOT
    / "reports/benchmarks/amd-vulkan/20260815T205128Z"
    / "holoscript-doeproof-cli-filesystem-diagnostic.json"
)
BWRAP_REPORT = (
    ROOT
    / "reports/benchmarks/amd-vulkan/20260815T212816Z"
    / "holoscript-doeproof-cli-linux-bwrap-diagnostic.json"
)


# Reviewed Fawn snapshots containing both the diagnostic and the source blobs
# it hashes. The upstream HoloScript commit identifies a different repository.
CLI_SNAPSHOT = "1c189b1f6dd44b887bfa58450c490586452c7960"
REVIEWED_SNAPSHOTS = {
    REPORT: "929ca998b1ef6414d2db09d2f06ffc85f6d05e77",
    CLI_REPORT: CLI_SNAPSHOT,
    FILESYSTEM_REPORT: CLI_SNAPSHOT,
    BWRAP_REPORT: CLI_SNAPSHOT,
}


def pinned_blob(commit: str, path: str) -> bytes:
    return subprocess.run(
        ["git", "cat-file", "blob", f"{commit}:{path}"], cwd=ROOT,
        env={**os.environ, "GIT_NO_LAZY_FETCH": "1"}, check=True, capture_output=True,
    ).stdout


class HoloScriptDoeProofLoaderIntegrationTests(unittest.TestCase):
    def test_frozen_plan_preserves_zero_credit_boundary(self) -> None:
        plan = json.loads(
            (HARNESS / "doeproof-loader-integration.plan.json").read_text()
        )
        self.assertEqual(set(plan["lanes"]), {"W0", "D0"})
        self.assertEqual(
            plan["expectedComparableSha256"],
            "51cda2a94da7edad85499240ac8cc5744598fb4227f4f38535a8c97880f585f4",
        )
        self.assertIn("runtime-ownership credit", plan["nonGoals"])
        self.assertIn("performance interpretation", plan["nonGoals"])
        self.assertIn(
            "reinterpretation of the terminal HoloScript ownership decision",
            plan["nonGoals"],
        )

    def test_public_loader_is_the_only_webgpu_substitution_seam(self) -> None:
        runner = (HARNESS / "run-doeproof-loader-integration.mjs").read_text()
        self.assertIn("packages/doe-gpu/src/node-webgpu-loader.js", runner)
        self.assertIn("runGovernedNodeWebGPUProcess", runner)
        self.assertIn("validateGovernedNodeWebGPUProcessReceipt", runner)
        self.assertNotIn("node:child_process", runner)
        self.assertNotIn("provider-loader.mjs", runner)

    def test_reviewed_loader_retains_diagnostic_credit_boundary(self) -> None:
        report = json.loads(REPORT.read_text())
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["decision"]["publicDoeProofLoader"], "authorized")
        self.assertEqual(report["decision"]["publicDoeProofProcess"], "authorized")
        self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
        self.assertFalse(report["decision"]["performanceCredit"])
        self.assertFalse(report["decision"]["releaseCredit"])

    def test_cli_plan_and_reviewed_diagnostic_preserve_boundary(self) -> None:
        plan = json.loads(
            (HARNESS / "doeproof-cli-integration.plan.json").read_text()
        )
        self.assertEqual(
            set(plan["commands"]), {"run", "verify", "inspect", "compare", "replay"}
        )
        self.assertIn("runtime-ownership credit", plan["nonGoals"])

        report = json.loads(CLI_REPORT.read_text())
        self.assertEqual(report["status"], "passed")
        self.assertEqual(report["decision"]["publicDoeProofCli"], "authorized")
        self.assertFalse(report["comparison"]["performanceInterpretable"])
        self.assertFalse(report["comparison"]["runtimeOwnershipCredit"])
        self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
        self.assertFalse(report["decision"]["releaseCredit"])

    def test_real_application_filesystem_boundary_is_hash_bound(self) -> None:
        plan = json.loads(
            (HARNESS / "doeproof-cli-filesystem-integration.plan.json").read_text()
        )
        self.assertEqual(plan["filesystemMode"], "node-permission-read-only")
        self.assertIn("operating-system dependency sealing", plan["nonGoals"])
        self.assertIn("hardware eligibility", plan["nonGoals"])

        report = json.loads(FILESYSTEM_REPORT.read_text())
        self.assertEqual(report["status"], "passed")
        self.assertEqual(
            report["decision"]["realApplicationNodePermissionBoundary"],
            "authorized",
        )
        self.assertFalse(report["decision"]["hardwareCredit"])
        self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
        self.assertEqual(
            report["permissionBoundary"]["rendererAttestation"],
            "omitted-by-node-permission",
        )

    def test_linux_workspace_sealing_is_real_and_narrowly_claimed(self) -> None:
        plan = json.loads(
            (HARNESS / "doeproof-cli-linux-bwrap-integration.plan.json").read_text()
        )
        self.assertEqual(
            plan["boundary"]["workspace"],
            "only hash-bound files declared by the harness",
        )
        self.assertIn(
            "complete base-operating-system dependency closure", plan["nonGoals"]
        )

        report = json.loads(BWRAP_REPORT.read_text())
        self.assertEqual(report["status"], "passed")
        self.assertTrue(report["decision"]["workspaceSealingCredit"])
        self.assertFalse(
            report["decision"]["completeOsDependencyClosureCredit"]
        )
        self.assertFalse(report["decision"]["runtimeOwnershipCredit"])
        self.assertFalse(report["decision"]["performanceCredit"])
        probe = report["sandbox"]["visibilityProbe"]
        self.assertTrue(probe["declaredFileVisible"])
        self.assertFalse(probe["undeclaredCanaryVisible"])
        self.assertEqual(probe["networkInterfaces"], ["lo"])
        self.assertEqual(
            report["sandbox"]["environment"],
            {
                "VK_DRIVER_FILES": "/usr/share/vulkan/icd.d/radeon_icd.json",
                "VK_LOADER_LAYERS_DISABLE": "~all~",
            },
        )
        writable_paths = {
            report["lanes"]["W0"]["writablePath"],
            report["lanes"]["D0"]["writablePath"],
            report["replay"]["writablePath"],
        }
        self.assertEqual(len(writable_paths), 3)
        system_access = report["systemAccessObservation"]
        self.assertEqual(system_access["credit"], "diagnostic-only")
        self.assertFalse(system_access["completeOsDependencyClosure"])
        self.assertEqual(
            system_access["vulkan"]["openedIcdManifests"],
            ["/usr/share/vulkan/icd.d/radeon_icd.json"],
        )
        self.assertEqual(system_access["vulkan"]["openedLayerLibraries"], [])

    def test_reviewed_reports_bind_their_pinned_plan_and_implementation(self) -> None:
        for path, commit in REVIEWED_SNAPSHOTS.items():
            with self.subTest(report=path.name):
                retained = path.read_bytes()
                self.assertEqual(retained, pinned_blob(commit, path.relative_to(ROOT).as_posix()))
                report = json.loads(retained)
                for reference in (report["plan"], *report["implementation"].values()):
                    source = pinned_blob(commit, reference["path"])
                    self.assertEqual(hashlib.sha256(source).hexdigest(), reference["sha256"], reference["path"])

    def test_local_raw_evidence_matches_reviewed_digests_when_available(self) -> None:
        missing = []
        for path in REVIEWED_SNAPSHOTS:
            report = json.loads(path.read_bytes())
            references = [report["evidence"]]
            if path == BWRAP_REPORT:
                references.append(report["systemAccessObservation"]["tracedEvidence"])
            for reference in references:
                raw = ROOT / reference["path"]
                if raw.is_file():
                    self.assertEqual(hashlib.sha256(raw.read_bytes()).hexdigest(), reference["sha256"], reference["path"])
                else:
                    missing.append(reference["path"])
        if missing:
            self.skipTest("Raw evidence unavailable locally: " + ", ".join(missing))


if __name__ == "__main__":
    unittest.main()
