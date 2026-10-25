"""Quarantine selection controls; no backend execution claim."""
import argparse
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from bench.tools.build_wgsl_backend_matrix import build_row, parse_args


class QuarantineMatrixTests(unittest.TestCase):
    def test_csl_receipts_require_explicit_selection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'shader.wgsl').write_text('@compute @workgroup_size(1) fn main() {}')
            fixture = {'id': 'control', 'kernelPattern': 'control',
                       'evidence': {'sourceWgslPath': 'shader.wgsl'}}
            args = argparse.Namespace(artifact_root='artifacts', emit_msl_executable='',
                                      emit_hlsl_executable='', emit_dxil_executable='', include_csl=False)
            with patch('bench.tools.build_wgsl_backend_matrix.csl_backend',
                       return_value={'status': 'blocked', 'reason': 'actual retained rejection'}) as csl:
                native = build_row(root, fixture, args)
                csl.assert_not_called()
                self.assertEqual(native['backends']['csl']['status'], 'not_wired')
                args.include_csl = True
                optional = build_row(root, fixture, args)
                csl.assert_called_once_with(root, fixture['evidence'])
                self.assertEqual(optional['backends']['csl']['status'], 'blocked')
                for backend in ['vulkan_spirv', 'metal_msl', 'd3d12_hlsl', 'd3d12_dxil']:
                    self.assertEqual(native['backends'][backend], optional['backends'][backend])

    def test_command_default_is_native(self):
        with patch('sys.argv', ['build_wgsl_backend_matrix.py']):
            self.assertFalse(parse_args().include_csl)
        with patch('sys.argv', ['build_wgsl_backend_matrix.py', '--include-csl']):
            self.assertTrue(parse_args().include_csl)


if __name__ == '__main__':
    unittest.main()
