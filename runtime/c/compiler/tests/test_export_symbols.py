#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Darwin build preparation, not a Darwin compiler or SDK qualification."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('export_symbols', ROOT/'tools/export_symbols.py')
EXPORTS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORTS)


class ExportSymbols(unittest.TestCase):
    def test_one_canonical_symbol_authority(self):
        names = EXPORTS.symbols((ROOT/'src/exports.map').read_text())
        output = subprocess.check_output(['python3', str(ROOT/'tools/export_symbols.py'),
                                          str(ROOT/'src/exports.map')], text=True)
        self.assertEqual(output.splitlines(), ['_'+name for name in names])
        self.assertEqual(len(names), 19)
        self.assertEqual(len(names), len(set(names)))

    def test_reject_unrecognized_or_expanded_map(self):
        for text in ['{global:*;local:*;};', '{global:f;f;local:*;};',
                     '{global:f local:*;};', '{global:;local:*;};',
                     '{global:f;local:g;};', '{global:f;local:*;}; extra',
                     'VERSION {global:f;local:*;};']:
            with self.assertRaises(ValueError):
                EXPORTS.symbols(text)
        self.assertEqual(EXPORTS.symbols('/* comment */{ global: f; g; local: *; };'), ['f','g'])

    def test_platform_recipes_keep_export_selection_explicit(self):
        for platform in ['Linux','Darwin']:
            output = subprocess.check_output(['make','-n','shared','SHARED_PLATFORM='+platform,
                'SHARED_LIBRARY=/tmp/doe-export-dry-run/libcompiler.so',
                'TMPDIR=/tmp/doe-export-dry-run/tmp'], cwd=ROOT, text=True)
            if platform == 'Darwin':
                self.assertIn('-dynamiclib', output)
                self.assertIn('-install_name,@rpath/libcompiler.so', output)
                self.assertIn('-exported_symbols_list,', output)
                self.assertIn('tools/export_symbols.py src/exports.map', output)
                self.assertNotIn('--version-script', output)
            else:
                self.assertIn('-fPIC -shared', output)
                self.assertIn('--version-script=src/exports.map', output)
                self.assertNotIn('-dynamiclib', output)

    def test_darwin_install_name_override(self):
        output = subprocess.check_output(['make', '-n', 'shared', 'SHARED_PLATFORM=Darwin',
            'SHARED_LIBRARY=/tmp/doe-export-dry-run/libcompiler.dylib',
            'SHARED_INSTALL_NAME=@rpath/owned/libcompiler.dylib',
            'TMPDIR=/tmp/doe-export-dry-run/tmp'], cwd=ROOT, text=True)
        self.assertIn('-install_name,@rpath/owned/libcompiler.dylib', output)
        self.assertNotIn('-install_name,/tmp/', output)


if __name__ == '__main__':
    unittest.main()
