#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Host source-contract checks, not a Windows Dawn producer acceptance claim."""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
import os
import hashlib
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_windows_dawn import DAWN_REVISION, abi_probe, deterministic_git_checkout, verify_header_bytes  # noqa: E402


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

    def test_checkout_config_is_process_scoped_and_preserves_inherited_options(self):
        with patch.dict(os.environ, {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': 'test.key',
                                     'GIT_CONFIG_VALUE_0': 'retained'}, clear=True):
            deterministic_git_checkout()
            self.assertEqual(os.environ['GIT_CONFIG_COUNT'], '3')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_0'], 'retained')
            self.assertEqual(os.environ['GIT_CONFIG_KEY_1'], 'core.autocrlf')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_1'], 'false')
            self.assertEqual(os.environ['GIT_CONFIG_KEY_2'], 'core.eol')
            self.assertEqual(os.environ['GIT_CONFIG_VALUE_2'], 'lf')

    def test_header_bytes_are_never_normalized_to_pass(self):
        blob = b'header\n'
        expected = hashlib.sha256(blob).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / 'webgpu.h'
            with patch('build_windows_dawn.subprocess.run', return_value=SimpleNamespace(stdout=blob)), \
                 patch('build_windows_dawn.capture', return_value='a' * 40):
                header.write_bytes(blob)
                verify_header_bytes(header, expected, root)
                header.write_bytes(b'header\r\n')
                with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                    verify_header_bytes(header, expected, root)
                evidence = json.loads((root / 'header-integrity.json').read_text())
                self.assertEqual(evidence['checkout']['crlf'], 1)
                self.assertEqual(evidence['gitBlob']['crlf'], 0)
                self.assertEqual(evidence['gitBlob']['sha256'], expected)

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
