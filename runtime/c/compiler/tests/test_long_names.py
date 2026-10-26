#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exact long source names across compiler targets, not device qualification."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from test_cli import decode


class LongNames(unittest.TestCase):
    def test_source_spans_and_debug_names(self):
        for length in (63, 64, 65, 257):
            names = [prefix + 'x' * (length - 1) for prefix in 'ABCDEF']
            structure, alias, resource, entry, helper, shared = names
            source = (
                f'struct {structure} {{v:u32}} alias {alias}={structure};'
                f'@group(0) @binding(0) var<storage,read_write> {resource}:array<u32>;'
                f'var<workgroup> {shared}:array<u32,1>;'
                f'fn {helper}()->u32{{let a={structure}(7u);let b={alias}(9u);return a.v+b.v;}}'
                f'@compute @workgroup_size(1) fn {entry}(){{{shared}[0]={helper}();'
                f'{resource}[0]={shared}[0];}}')
            for target in ('spirv', 'msl', 'hlsl'):
                with self.subTest(length=length, target=target):
                    with tempfile.TemporaryDirectory(prefix='doe-long-names-') as directory:
                        path = Path(directory)
                        shader, output = path / 'source.wgsl', path / 'output'
                        shader.write_text(source)
                        result = subprocess.run(
                            [os.environ['DOE_COMPILER_CLI'], '--shader-path', str(shader),
                             '--out', str(output), '--target', target],
                            capture_output=True, text=True, timeout=10)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        self.assertTrue(output.exists())
                        if target == 'spirv':
                            validation = subprocess.run(
                                [os.environ['SPIRV_VAL'], '--target-env', 'vulkan1.1', str(output)],
                                capture_output=True, text=True, timeout=10)
                            self.assertEqual(validation.returncode, 0, validation.stderr)
                            actual = []
                            for opcode, operands in decode(output.read_bytes()):
                                if opcode == 5:  # OpName: id, exact NUL-terminated UTF-8 bytes.
                                    data = struct.pack('<' + 'I' * (len(operands) - 1), *operands[1:])
                                    actual.append(data.split(b'\0', 1)[0].decode('ascii'))
                            # Ordinary helper calls may be inlined; only surviving
                            # declarations promise OpName identity.
                            for name in (resource, entry, shared):
                                self.assertIn(name, actual)
                        else:
                            text = output.read_text()
                            self.assertTrue(text)
                            if length > 63:
                                self.assertIn('doe_long_entry_', text)


if __name__ == '__main__':
    unittest.main()
