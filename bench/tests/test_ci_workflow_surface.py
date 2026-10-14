#!/usr/bin/env python3
"""Repository-level contracts for the GitHub Actions surface."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_ROOT = REPO_ROOT / ".github" / "workflows"

AUTOMATIC_WORKFLOWS = {
    "agent-sync.yml",
    "doe-gpu-native-freshness.yml",
    "lean-check.yml",
    "nightly-quirk-mining.yml",
    "publication-hygiene.yml",
    "webgpu-package-surface.yml",
    "wgsl-compiler.yml",
}

MANUAL_WORKFLOWS = {
    "amd-vulkan-smoke.yml",
    "doe-gpu-native-release-candidate.yml",
    "dropin-compat.yml",
    "fawn-matrix-physical-runner.yml",
    "macos-browser-refresh.yml",
    "release-claim-trends.yml",
    "release-gates.yml",
    "windows-d3d12-qualification.yml",
}

REUSABLE_WORKFLOWS = {
    "fawn-matrix-release-passport.yml": {"workflow_call", "workflow_dispatch"},
}

STALE_PATH_PATTERNS = {
    r"\bcd zig\b": "runtime/zig must be the workflow working directory",
    r"(?<!runtime/)\bzig/zig-out": "runtime artifacts live under runtime/zig/zig-out",
    r"\bbench/run_": "benchmark runners live under bench/runners",
    r"\bbench/dropin_gate\.py": "the drop-in gate lives under bench/drop-in",
    r"\bbench/bootstrap_dawn\.py": "Dawn bootstrap lives under bench/tools",
    r"\bbench/browser_claim_gate\.py": "browser gates live under bench/browser",
    r"\bbench/cleanup_out\.py": "cleanup tooling lives under bench/tools",
    r"\bbench/build_test_inventory_dashboard\.py": "dashboard tooling lives under bench/tools",
    r"\bnursery/(?:chromium|webgpu)\b": "active browser and package surfaces left nursery",
}


def workflow_text(name: str) -> str:
    return (WORKFLOW_ROOT / name).read_text(encoding="utf-8")


def workflow_triggers(text: str) -> set[str]:
    lines = text.splitlines()
    try:
        start = lines.index("on:") + 1
    except ValueError as exc:
        raise AssertionError("workflow must use a block-style on section") from exc

    triggers: set[str] = set()
    for line in lines[start:]:
        if line and not line.startswith((" ", "\t")):
            break
        match = re.match(r"^  ([a-z_]+):", line)
        if match:
            triggers.add(match.group(1))
    return triggers


class CiWorkflowSurfaceTests(unittest.TestCase):
    def test_workflow_inventory_is_intentional(self) -> None:
        actual = {path.name for path in WORKFLOW_ROOT.glob("*.yml")}
        expected = AUTOMATIC_WORKFLOWS | MANUAL_WORKFLOWS | set(REUSABLE_WORKFLOWS)
        self.assertEqual(actual, expected)

    def test_hardware_and_vendor_workflows_are_manual(self) -> None:
        for name in MANUAL_WORKFLOWS:
            with self.subTest(workflow=name):
                self.assertEqual(workflow_triggers(workflow_text(name)), {"workflow_dispatch"})

    def test_reusable_workflow_triggers_are_explicit(self) -> None:
        for name, expected in REUSABLE_WORKFLOWS.items():
            with self.subTest(workflow=name):
                self.assertEqual(workflow_triggers(workflow_text(name)), expected)

    def test_automatic_self_hosted_job_is_manual_only(self) -> None:
        text = workflow_text("doe-gpu-native-freshness.yml")
        self.assertIn("if: github.event_name == 'workflow_dispatch'", text)

    def test_physical_workflows_bind_revision_runner_and_failed_evidence(self) -> None:
        for name in (
            "amd-vulkan-smoke.yml",
            "doe-gpu-native-release-candidate.yml",
            "fawn-matrix-physical-runner.yml",
            "release-gates.yml",
            "windows-d3d12-qualification.yml",
        ):
            text = workflow_text(name)
            with self.subTest(workflow=name):
                self.assertIn("exact_revision:", text)
                self.assertIn("approved_runner_name:", text)
                self.assertIn("ref: ${{ inputs.exact_revision }}", text)
                self.assertIn("RUNNER_NAME", text)
                self.assertIn("^[0-9a-f]{40}$", text)
                self.assertRegex(
                    text,
                    r"(?s)if: always\(\).*?uses: actions/upload-artifact@",
                )
                self.assertIn("SHA256SUMS", text)

    def test_fawn_physical_runner_preserves_failures_unsigned(self) -> None:
        text = workflow_text("fawn-matrix-physical-runner.yml")
        required_fragments = (
            "continue-on-error: true",
            "unsigned_review_required",
            "unset DOE_PROOF_SIGNING_KEY",
        )
        for fragment in required_fragments:
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, text)

    def test_workflows_only_reference_current_repo_layout(self) -> None:
        for path in WORKFLOW_ROOT.glob("*.yml"):
            text = path.read_text(encoding="utf-8")
            for pattern, reason in STALE_PATH_PATTERNS.items():
                with self.subTest(workflow=path.name, pattern=pattern):
                    self.assertIsNone(re.search(pattern, text), reason)


if __name__ == "__main__":
    unittest.main()
