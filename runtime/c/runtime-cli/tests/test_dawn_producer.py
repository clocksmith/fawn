#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host source-contract checks, not a Windows Dawn producer acceptance claim."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_windows_dawn import DAWN_REVISION, abi_probe  # noqa: E402


class ProducerContract(unittest.TestCase):
    def test_pinned_probe_is_complete_and_reproducible(self):
        repo = ROOT.parents[2]
        policy = json.loads((repo / 'config/webgpu-abi-source.json').read_text())['source']
        header = (repo / policy['path']).read_text()
        probe = abi_probe(header)
        self.assertEqual(probe, abi_probe(header))
        self.assertEqual(probe.count('printf('), 989)
        self.assertIn('offsetof(WGPUComputePassDescriptor, timestampWrites)', probe)
        self.assertIn('WGPUFeatureName_ShaderF16=', probe)
        self.assertEqual(len(DAWN_REVISION), 40)

    def test_incomplete_probe_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            abi_probe('typedef struct WGPUExample { int field; } WGPUExample;')

    def test_windows_scripts_import_without_execution(self):
        for name in ['build_windows.py', 'build_windows_dawn.py']:
            spec = importlib.util.spec_from_file_location('check_' + name, ROOT / 'tools' / name)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            self.assertTrue(callable(module.main))


if __name__ == '__main__':
    unittest.main()
