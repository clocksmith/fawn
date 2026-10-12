"""Delegated workflows preserve process identity, arguments and failed outcomes."""
from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from bench.cli import WORKFLOW_COMMANDS

ROOT = Path(__file__).resolve().parents[2]


class WorkflowCliTests(unittest.TestCase):
    def test_real_workflows_expose_their_existing_help(self) -> None:
        for name in WORKFLOW_COMMANDS:
            with self.subTest(workflow=name):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "bench/cli.py"), name, "--help"],
                    cwd=ROOT, capture_output=True, text=True, timeout=30, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("usage:", result.stdout.lower())

    def test_delegate_preserves_arguments_environment_and_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            probe = Path(temporary) / "probe.py"
            probe.write_text(
                "import json, os, sys\n"
                "print(json.dumps({'args': sys.argv[1:], 'cwd': os.getcwd(), "
                "'pid': os.getpid(), 'policy': os.environ['DOE_CLI_TEST_POLICY']}))\n"
                "raise SystemExit(17)\n", encoding="utf-8",
            )
            for name in WORKFLOW_COMMANDS:
                with self.subTest(workflow=name):
                    arguments = ["--out", "path with spaces", "--policy", "unchanged.json"]
                    launch = (
                        "import sys\nfrom bench import cli\n"
                        f"cli.WORKFLOW_COMMANDS[{name!r}] = "
                        f"({[sys.executable, str(probe)]!r}, 'probe')\n"
                        f"sys.argv = {['bench/cli.py', name, *arguments]!r}\n"
                        "raise SystemExit(cli.main())\n"
                    )
                    with subprocess.Popen(
                        [sys.executable, "-c", launch], cwd=temporary,
                        env={**os.environ, "PYTHONPATH": str(ROOT),
                             "DOE_CLI_TEST_POLICY": "frozen"},
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                    ) as process:
                        stdout, stderr = process.communicate(timeout=30)
                        self.assertEqual(process.returncode, 17, stderr)
                        observed = json.loads(stdout)
                        self.assertEqual(observed["args"], arguments)
                        self.assertEqual(observed["cwd"], str(ROOT))
                        self.assertEqual(observed["pid"], process.pid)
                        self.assertEqual(observed["policy"], "frozen")


if __name__ == "__main__":
    unittest.main()
