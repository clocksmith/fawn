#!/usr/bin/env python3
"""Package plan supervisor must never report a failed child as success."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNNER_CORE_URL = (
    REPO_ROOT / "bench" / "executors" / "package-webgpu" / "runner-core.js"
).resolve().as_uri()
NODE_RUNNER = REPO_ROOT / "bench" / "executors" / "run-node-webgpu-plan.js"
BUN_RUNNER = REPO_ROOT / "bench" / "executors" / "run-bun-webgpu-plan.js"
PLAN = (
    REPO_ROOT
    / "bench"
    / "plans"
    / "package-developer"
    / "package_queue_submit_completion.plan.json"
)


def _failure_reasons(meta: object) -> list[str]:
    script = (
        f"import {{ traceMetaExecutionFailureReasons }} from '{RUNNER_CORE_URL}';"
        "const meta = JSON.parse(process.argv[1]);"
        "process.stdout.write(JSON.stringify("
        "traceMetaExecutionFailureReasons(meta)));"
    )
    completed = subprocess.run(
        ["node", "--input-type=module", "-e", script, json.dumps(meta)],
        check=True,
        capture_output=True,
        text=True,
    )
    return json.loads(completed.stdout)


@unittest.skipIf(shutil.which("node") is None, "node is required")
class PackageRunnerSupervisorTests(unittest.TestCase):
    def test_completed_work_has_no_failure_reasons(self) -> None:
        self.assertEqual(
            _failure_reasons(
                {"executionErrorCount": 0, "executionSuccessCount": 2}
            ),
            [],
        )

    def test_error_zero_success_and_provider_failure_are_failures(self) -> None:
        reasons = _failure_reasons(
            {
                "executionErrorCount": 1,
                "executionSuccessCount": 0,
                "providerFailureReason": "runner_error",
            }
        )
        self.assertIn("executionErrorCount=1", reasons)
        self.assertIn("executionSuccessCount=0", reasons)
        self.assertIn("providerFailureReason=runner_error", reasons)

    def test_unsupported_and_missing_meta_are_failures(self) -> None:
        self.assertIn(
            "executionUnsupportedCount=1",
            _failure_reasons(
                {"executionUnsupportedCount": 1, "executionSuccessCount": 0}
            ),
        )
        self.assertEqual(_failure_reasons(None), ["trace-meta-missing"])

    def _run_with_broken_library(self, runtime: str, runner: Path) -> None:
        # A library that cannot load must leave a failed receipt and a
        # nonzero exit; it previously exited 0 with a 0 ms "success".
        with tempfile.TemporaryDirectory(prefix="doe-broken-lib-") as tmpdir:
            tmp = Path(tmpdir)
            broken = tmp / "broken-webgpu.lib"
            broken.write_text("not a shared library\n", encoding="utf-8")
            meta_path = tmp / "run.meta.json"
            completed = subprocess.run(
                [
                    runtime,
                    str(runner),
                    "--provider",
                    "doe",
                    "--plan",
                    str(PLAN),
                    "--trace-meta",
                    str(meta_path),
                    "--trace-jsonl",
                    str(tmp / "run.ndjson"),
                    "--workload",
                    "package_queue_submit_completion",
                ],
                cwd=REPO_ROOT,
                env={**os.environ, "DOE_WEBGPU_LIB": str(broken)},
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertNotEqual(completed.returncode, 0, completed.stderr)
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        self.assertGreater(meta["executionErrorCount"], 0)
        self.assertEqual(meta["executionSuccessCount"], 0)

    def test_node_broken_provider_library_fails_the_run(self) -> None:
        self._run_with_broken_library("node", NODE_RUNNER)

    @unittest.skipIf(shutil.which("bun") is None, "bun is required")
    def test_bun_broken_provider_library_fails_the_run(self) -> None:
        self._run_with_broken_library("bun", BUN_RUNNER)


if __name__ == "__main__":
    unittest.main()
