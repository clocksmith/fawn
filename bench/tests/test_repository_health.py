"""Health orchestration must preserve canonical failures and owned-source checks."""

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "repository_health", REPO_ROOT / "scripts/check-health.py"
)
assert SPEC is not None and SPEC.loader is not None
HEALTH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HEALTH)


class RepositoryHealthTests(unittest.TestCase):
    def test_canonical_gate_failures_remain_failures(self) -> None:
        gates = (
            ("runtime/zig/tools/check_line_limits.py", HEALTH.check_zig_sizes),
            ("bench/gates/schema_gate.py", HEALTH.check_schema_validation),
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with patch.object(HEALTH, "REPO_ROOT", root):
                for relative, check in gates:
                    with self.subTest(gate=relative):
                        gate = root / relative
                        gate.parent.mkdir(parents=True, exist_ok=True)
                        gate.write_text(
                            "import sys\nprint('invalid governed input', file=sys.stderr)\n"
                            "sys.exit(2)\n", encoding="utf-8"
                        )
                        self.assertEqual(check(), ["invalid governed input"])
                        gate.write_text("raise SystemExit(0)\n", encoding="utf-8")
                        self.assertEqual(check(), [])
                        gate.unlink()
                        self.assertTrue(check(), "missing gate must not pass")

    def test_owned_oversize_source_rejects_without_scanning_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bench = root / "bench"
            paths = (
                "tools/oversize.py", "out/run/generated.py", "vendor/library.py",
                "vendor-dawn/library.py", "node_modules/dependency/tool.py",
                ".venv/tool.py", "__pycache__/generated.py",
            )
            for relative in paths:
                path = bench / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("# line\n" * (HEALTH.PYTHON_MAX_LINES + 1), encoding="utf-8")
            with (
                patch.object(HEALTH, "REPO_ROOT", root),
                patch.object(HEALTH, "PYTHON_DIRS", [bench]),
            ):
                failures = HEALTH.check_python_sizes()
            self.assertEqual(len(failures), 1)
            self.assertIn("bench/tools/oversize.py", failures[0])


if __name__ == "__main__":
    unittest.main()
