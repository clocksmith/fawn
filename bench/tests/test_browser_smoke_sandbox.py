"""Reject contradictory sandbox requests before the smoke runner launches."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WRAPPER = ROOT / "browser/chromium/scripts/run-smoke.sh"


class BrowserSmokeSandboxTests(unittest.TestCase):
    def run_args(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(WRAPPER), *args], cwd=ROOT, capture_output=True,
            text=True, timeout=10, check=False,
        )

    def test_rejects_sandbox_disabling_overrides(self) -> None:
        for flag in (
            "--no-sandbox", "--no-sandbox=true", "--disable-gpu-sandbox",
            "--disable-setuid-sandbox", "--disable-namespace-sandbox",
            "--disable-seccomp-filter-sandbox", "--single-process",
        ):
            with self.subTest(flag=flag):
                result = self.run_args(
                    "--sandbox", "true", "--chrome-arg", flag,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("conflicts with sandbox-disabling", result.stderr)

    def test_explicit_disabled_mode_preserves_diagnostic_arguments(self) -> None:
        result = self.run_args(
            "--sandbox", "false", "--chrome-arg", "--no-sandbox",
            "--api-surface", "invalid",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--api-surface must be", result.stderr)
        self.assertNotIn("sandbox-disabling", result.stderr)

    def test_rejects_invalid_sandbox_value(self) -> None:
        result = self.run_args("--sandbox", "maybe")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--sandbox", result.stderr)


if __name__ == "__main__":
    unittest.main()
