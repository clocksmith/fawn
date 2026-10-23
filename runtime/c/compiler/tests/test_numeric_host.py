#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Actual host arithmetic preconditions, not fabricated Darwin/ARM execution."""
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NumericHost(unittest.TestCase):
    def check_flags(self, flags, expected=None):
        result = subprocess.run(['gcc','-std=c11','-fsyntax-only',*flags,
                                 str(ROOT/'src/numeric_math.c')], capture_output=True, text=True)
        if expected is None:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(expected, result.stderr)

    def test_declared_width_ieee_host(self):
        self.check_flags(['-fno-fast-math','-ffp-contract=off','-frounding-math'])

    def test_fast_or_finite_only_math_rejects(self):
        for flag in ['-ffast-math','-ffinite-math-only']:
            self.check_flags([flag], 'requires strict, finite-aware')

    def test_actual_excess_precision_mode_rejects(self):
        self.check_flags(['-mfpmath=387'], 'requires declared widths and gradual underflow')


if __name__ == '__main__':
    unittest.main()
