#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Adapter pool growth, distinct from fixed legacy projections and GPU execution."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

CLI = os.environ['DOE_COMPILER_CLI']
FAULT_CLI = os.environ.get('DOE_COMPILER_FAULT_CLI')


def updates(count=1100):
    return '@compute @workgroup_size(1) fn main(){var x=0u;' + 'x=x+1u;' * count + '}'


class Growth(unittest.TestCase):
    def invoke(self, source, target='spirv', extra=(), fault=None):
        with tempfile.TemporaryDirectory(prefix='doe-cli-growth-') as directory:
            root = Path(directory)
            shader, output = root / 'input.wgsl', root / 'output'
            shader.write_text(source)
            env = os.environ.copy()
            if fault is not None:
                env.update(DOE_CLI_FAULT='malloc', DOE_CLI_FAULT_CALL=str(fault))
            command = [FAULT_CLI if fault is not None else CLI, '--shader-path', str(shader),
                       '--out', str(output), '--target', target, *extra]
            result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=30)
            if result.returncode:
                self.assertFalse(output.exists(), result.stderr)
                self.assertEqual(sorted(p.name for p in root.iterdir()), ['input.wgsl'])
            else:
                self.assertTrue(output.stat().st_size)
            return result

    def test_source_pools_all_targets(self):
        sources = [updates(), updates(2200),
                   ''.join(f'struct S{i}{{x:u32}}\n' for i in range(300)) + updates(1),
                   ''.join(f'@group({i//32}) @binding({i%32}) var<storage,read> b{i}:array<u32>;\n'
                           for i in range(40)) + updates(1)]
        for target in ('spirv', 'msl', 'hlsl'):
            for source in sources:
                with self.subTest(target=target, size=len(source)):
                    result = self.invoke(source, target)
                    self.assertEqual(result.returncode, 0, result.stderr)

    def test_specialized_runtime_route(self):
        source = 'override size:u32=1u;' + updates().replace('@workgroup_size(1)', '@workgroup_size(size)')
        result = self.invoke(source, extra=('--mode', 'vulkan-compute-runtime', '--overrides', 'size=2'))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_semantic_error_after_growth_never_publishes(self):
        source = updates().replace('}', 'missing=1u;}')
        for target in ('spirv', 'msl', 'hlsl'):
            result = self.invoke(source, target)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('category=4', result.stderr)

    def test_source_bound_preserved(self):
        result = self.invoke(' ' * (2 * 1024 * 1024 + 1))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('2 MiB source limit', result.stderr)

    @unittest.skipUnless(FAULT_CLI, 'fault-injected CLI is an explicit separate build')
    def test_all_growth_allocations_clean_up(self):
        # The existing fault wrapper asserts zero outstanding allocations at exit.
        reached_success = False
        for index in range(1, 33):
            result = self.invoke(updates(), fault=index)
            self.assertIn(result.returncode, (0, 1), result.stderr)
            if result.returncode == 0:
                reached_success = True
                break
        self.assertTrue(reached_success, 'allocation sweep did not reach the no-failure control')


if __name__ == '__main__':
    unittest.main()
