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
    def config(self, directory: Path, *, launch: dict | None = None) -> Path:
        path = directory / "config.json"
        path.write_text(json.dumps({
            "lock": str(directory / "gpu.lock"),
            "reps": 12,
            "pages": ["compute_atomic_canvas", "heavy_particles"],
            **({"launch": launch} if launch is not None else {}),
            "arms": [{"id": arm, "browserPath": "/unused/browser", "args": [],
                      "expectDoe": arm == "fawn_doe"}
                     for arm in ["chrome", "fawn_doe", "fawn_dawn"]],
        }), encoding="utf-8")
        return path

    def run_browser(self, directory: Path, *arguments: str,
                    environment: dict[str, str] | None = None,
                    launch: dict | None = None) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(ROOT / "bench/cli.py"), "browser", "--config",
             str(self.config(directory, launch=launch)), "--out", str(directory / "results"),
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

    def test_headless_admission_is_explicit_and_invalid_modes_fail(self) -> None:
        cases = [({}, (), False), ({}, ("--headless",), True),
                 ({"headless": True}, (), True),
                 ({"headless": False}, ("--headless",), True)]
        for launch, flags, expected in cases:
            with self.subTest(launch=launch, flags=flags), tempfile.TemporaryDirectory() as temporary:
                result = self.run_browser(
                    Path(temporary), *flags, "--dry-run", launch=launch,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout).get("headless", False), expected)
        for mode in ("false", 1, None):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                result = self.run_browser(
                    Path(temporary), "--dry-run", launch={"headless": mode},
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("must be a boolean", result.stderr)

    @unittest.skipIf(os.name == "nt", "POSIX fake launcher control")
    def test_headless_launch_reaches_browser_without_requesting_focus(self) -> None:
        for headless in (False, True):
            with self.subTest(headless=headless), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                package = directory / "browser/chromium"
                module = package / "node_modules/playwright-core"
                module.mkdir(parents=True)
                (package / "package.json").write_text("{}", encoding="utf-8")
                (module / "index.js").write_text("""
const fs = require('node:fs');
exports.chromium = {launch: async options => {
  fs.writeFileSync(process.env.LAUNCH_OBSERVATION, JSON.stringify(options));
  let sessions = 0;
  const page = {
    on() {},
    async bringToFront() { throw Error('fixture requested focus'); },
    async goto() { throw Error('fixture reached navigation'); }
  };
  return {
    version() { return 'fixture'; },
    async newBrowserCDPSession() {
      if (++sessions > 1) throw Error('fixture CDP closed');
      return {send: async () => ({processInfo: []})};
    },
    async newContext() {
      return {addInitScript: async () => {}, newPage: async () => page};
    },
    async close() {}
  };
}};
""", encoding="utf-8")
                binary = directory / "browser-fixture"
                binary.write_bytes(b"fixture")
                lock = directory / "flock"
                lock.write_text('#!/bin/sh\nshift\nexec "$@"\n', encoding="utf-8")
                lock.chmod(0o755)
                config = json.loads(self.config(directory).read_text())
                config["launch"] = {"repoRoot": str(directory), "headless": headless}
                config["arms"] = [{"id": "fixture", "browserPath": str(binary),
                                   "args": [], "expectDoe": False}]
                config_path = directory / "config.json"
                config_path.write_text(json.dumps(config), encoding="utf-8")
                observed = directory / "observed.json"
                result = subprocess.run(
                    [sys.executable, str(ROOT / "bench/cli.py"), "browser",
                     "--config", str(config_path), "--out", str(directory / "results"),
                     "--pages", "compute_atomic_canvas", "--reps", "1"],
                    cwd=ROOT, capture_output=True, text=True, timeout=15,
                    env={**os.environ, "LAUNCH_OBSERVATION": str(observed),
                         "PATH": str(directory) + os.pathsep + os.environ["PATH"]},
                )
                self.assertNotEqual(result.returncode, 0)
                options = json.loads(observed.read_text())
                self.assertEqual(options["headless"], headless)
                self.assertTrue(options["chromiumSandbox"])
                receipt_path = directory / "results/launches/compute_atomic_canvas.fixture.r00.json"
                receipt = json.loads(receipt_path.read_text())
                errors = " ".join(f["error"] for f in receipt["failures"])
                self.assertIn("fixture reached navigation" if headless else "fixture requested focus", errors)

    def test_analysis_rejects_mixed_presentation_modes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            launches = directory / "launches"
            launches.mkdir()
            for rep, headless in enumerate((False, True)):
                receipt = {
                    "spec": {"page": "fixture", "arm": "chrome", "rep": rep,
                             "browserPath": "/fixture/browser", "args": [],
                             "headless": headless},
                    "ok": True,
                }
                (launches / f"r{rep}.json").write_text(json.dumps(receipt), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(ROOT / "bench/cli.py"), "browser-analyze", str(directory)],
                cwd=ROOT, capture_output=True, text=True, timeout=15,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Cannot combine headed and headless", result.stderr)
            self.assertFalse((directory / "summary.json").exists())

    def test_headless_cannot_resume_a_headed_schedule(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            planned = self.run_browser(directory, "--dry-run")
            output = directory / "results"
            output.mkdir()
            schedule = output / "schedule.json"
            schedule.write_text(planned.stdout, encoding="utf-8")
            original = schedule.read_bytes()
            resumed = self.run_browser(directory, "--headless")
            self.assertNotEqual(resumed.returncode, 0)
            self.assertIn("Schedule changed", resumed.stderr)
            self.assertEqual(schedule.read_bytes(), original)
            self.assertFalse((output / "launches").exists())

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
