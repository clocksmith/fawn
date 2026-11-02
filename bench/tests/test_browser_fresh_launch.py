"""Fresh-launch selection and scheduling work without launching a browser."""
from __future__ import annotations

from collections import Counter
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BrowserFreshLaunchTests(unittest.TestCase):
    def config(self, directory: Path) -> Path:
        path = directory / "config.json"
        path.write_text(json.dumps({
            "lock": str(directory / "gpu.lock"),
            "reps": 12,
            "pages": ["compute_atomic_canvas", "heavy_particles"],
            "arms": [{"id": arm, "browserPath": "/unused/browser", "args": [],
                      "expectDoe": arm == "fawn_doe"}
                     for arm in ["chrome", "fawn_doe", "fawn_dawn"]],
        }), encoding="utf-8")
        return path

    def run_browser(self, directory: Path, *arguments: str,
                    environment: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(ROOT / "bench/cli.py"), "browser", "--config",
             str(self.config(directory)), "--out", str(directory / "results"),
             *arguments], cwd=ROOT, capture_output=True, text=True,
            timeout=15, check=False, env=environment,
        )

    def test_single_page_iteration_has_only_three_launches(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            result = self.run_browser(directory, "--pages", "heavy_particles", "--reps", "1", "--dry-run")
            self.assertEqual(result.returncode, 0, result.stderr)
            plan = json.loads(result.stdout)
            self.assertEqual(plan["pages"], ["heavy_particles"])
            self.assertEqual(len(plan["schedule"]), 3)
            self.assertEqual({row["arm"] for row in plan["schedule"]}, {"chrome", "fawn_doe", "fawn_dawn"})
            self.assertFalse((directory / "results").exists())

    def test_six_repetitions_balance_arm_positions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = self.run_browser(Path(temporary), "--pages", "compute_atomic_canvas", "--reps", "6", "--dry-run")
            self.assertEqual(result.returncode, 0, result.stderr)
            counts = Counter((row["arm"], row["position"]) for row in json.loads(result.stdout)["schedule"])
            self.assertEqual(len(counts), 9)
            self.assertEqual(set(counts.values()), {2})

    def test_invalid_selection_and_repetitions_fail_before_output(self) -> None:
        for arguments in [("--pages", "missing"), ("--reps", "0"), ("--reps", "1.5")]:
            with self.subTest(arguments=arguments), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                result = self.run_browser(directory, *arguments, "--dry-run")
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((directory / "results").exists())

    @unittest.skipIf(os.name == "nt", "POSIX lock-process failure control")
    def test_failed_launch_process_is_not_success(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            lock = directory / "flock"
            lock.write_text("#!/bin/sh\nexit 17\n", encoding="utf-8")
            lock.chmod(0o755)
            result = self.run_browser(
                directory, "--pages", "compute_atomic_canvas", "--reps", "1",
                environment={**os.environ, "PATH": str(directory) + os.pathsep + os.environ["PATH"]},
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exit=17", result.stdout)
            self.assertIn("failed launches", result.stdout)


if __name__ == "__main__":
    unittest.main()
