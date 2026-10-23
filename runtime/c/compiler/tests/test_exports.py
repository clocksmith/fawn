#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Verify the shared object's sole public ABI and the interposition fixture."""
from __future__ import annotations

from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def defined_exports(binary: Path) -> set[str]:
    output = subprocess.run(
        ["nm", "-D", "--defined-only", str(binary)],
        check=True, capture_output=True, text=True,
    ).stdout
    return {line.split()[-1] for line in output.splitlines() if line.strip()}


class TestPublicExports(unittest.TestCase):
    def test_library_has_only_versioned_api_function(self) -> None:
        self.assertEqual(defined_exports(ROOT / "build/libdoe_compiler.so"),
                         {"doe_compiler_compile"})

    def test_interposition_fixture_exports_conflicting_host_symbol(self) -> None:
        self.assertIn("lex_source", defined_exports(ROOT / "build/interposition_consumer"))


if __name__ == "__main__":
    unittest.main()
