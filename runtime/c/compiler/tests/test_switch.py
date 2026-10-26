#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""WGSL switch semantics and three-target emission, not physical qualification."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_cli import decode

CLI = os.environ['DOE_COMPILER_CLI']
VALIDATOR = os.environ['SPIRV_VAL']
PREFIX = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
ENTRY = '@compute @workgroup_size(4) fn main(@builtin(global_invocation_id) id:vec3u)'


class Switch(unittest.TestCase):
    def check_source(self, source, valid=True):
        for target in ('spirv', 'msl', 'hlsl'):
            with self.subTest(target=target, source=source):
                with tempfile.TemporaryDirectory(prefix='doe-switch-') as directory:
                    path = Path(directory)
                    shader = path / 'shader.wgsl'
                    output = path / 'shader.out'
                    shader.write_text(source)
                    result = subprocess.run([CLI, '--shader-path', str(shader), '--out',
                                             str(output), '--target', target],
                                            capture_output=True, text=True, timeout=10)
                    if not valid:
                        self.assertNotEqual(result.returncode, 0)
                        self.assertFalse(output.exists())
                        continue
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertTrue(output.exists())
                    if target == 'spirv':
                        validation = subprocess.run([VALIDATOR, '--target-env', 'vulkan1.1',
                                                     str(output)], capture_output=True,
                                                    text=True, timeout=10)
                        self.assertEqual(validation.returncode, 0, validation.stderr)
                        self.assertTrue(any(op == 251 for op, _ in decode(output.read_bytes())))
                    else:
                        self.assertIn('switch', output.read_text())

    def test_selector_types_and_clauses(self):
        cases = [
            'switch id.x {case 0u:{out[0]=11u;} default:{out[0]=22u;}}',
            'switch i32(id.x) {default:{out[0]=9u;} case -1,0,1:{out[0]=7u;}}',
            'switch 1 {case 1u:{out[0]=7u;} default:{out[0]=9u;}}',
            'switch 1 {case 1i:{out[0]=7u;} default:{out[0]=9u;}}',
            'switch id.x {case 1u, default {out[0]=7u;}}',
            'switch id.x {case 0u,1u, {out[0]=7u;} default {out[0]=9u;}}',
            'const value=1u+2u; switch id.x {case value:{out[0]=7u;} default:{}}',
            'switch id.x {case 4294967295u:{} default:{}}',
            # WGSL minimum is abstract negation then materialization, not an
            # out-of-range positive i32 literal followed by negation.
            'switch i32(id.x) {case -2147483648:{} default:{}}',
            'switch id.x {case 0u:{let local=1u;out[0]=local;} '
            'default:{let local=2u;out[0]=local;}}',
        ]
        for body in cases:
            self.check_source(PREFIX + ENTRY + '{' + body + '}')

    def test_nested_control_and_behavior(self):
        cases = [
            'switch id.x {case 0u:{switch id.y {case 1u:{break;} default:{}} break;} default:{}}',
            'for(var i=0u;i<4u;i++){switch i {case 0u:{continue;} '
            'case 1u:{out[0]=7u;} default:{break;}} out[1]=i;}',
            'switch id.x {case 0u:{loop {out[0]=1u;break;} out[1]=2u;} default:{}}',
            'loop {continuing {switch 1 {case 1:{break;} default:{}} break if true;}}',
            'switch id.x {case 0u:{} default:{}} workgroupBarrier();',
            'switch 1u {case 1u:{workgroupBarrier();} default:{}}',
        ]
        for body in cases:
            self.check_source(PREFIX + ENTRY + '{' + body + '}')
        self.check_source(PREFIX + 'fn f(x:u32)->u32 {switch x {'
                          'case 0u:{return 3u;} default:{return 4u;}}}' +
                          ENTRY + '{out[id.x]=f(id.x);}')

    def test_invalid_selectors_and_defaults(self):
        cases = [
            'switch true {case true:{} default:{}}',
            'switch 1.0 {case 1.0:{} default:{}}',
            'switch vec2u(0) {default:{}}',
            'switch id.x {}',
            'switch id.x {case 0u:{}}',
            'switch id.x {default:{} default:{}}',
            'switch id.x {case default, default:{}}',
            'switch id.x {case 1u:{} case 1:{} default:{}}',
            'switch id.x {case 1u,1u:{} default:{}}',
            'switch id.x {case 1u+1u:{} case 2u:{} default:{}}',
            'switch id.x {case 1i:{} default:{}}',
            'switch id.x {case -1:{} default:{}}',
            'switch i32(id.x) {case -2147483648i:{} default:{}}',
            'switch id.x {case id.y:{} default:{}}',
            'let value=1u; switch id.x {case value:{} default:{}}',
            'switch id.x {case 0u:{fallthrough;} default:{}}',
            'switch id.x {case 0u:{continue;} default:{}}',
            'loop {continuing {switch 1 {default:{continue;}} break if true;}}',
            'switch id.x {case 0u:{let local=1u;} default:{out[0]=local;}}',
            'switch id.x {case 0u:{workgroupBarrier();} default:{}}',
            'switch id.x {case 0u:{return;} default:{}} workgroupBarrier();',
            'var b=true; switch id.x {case 0u:{b=false;} default:{}} '
            'if(b){workgroupBarrier();}',
        ]
        for body in cases:
            self.check_source(PREFIX + ENTRY + '{' + body + '}', False)
        self.check_source('override key:u32=1u;' + PREFIX + ENTRY +
                          '{switch id.x {case key:{} default:{}}}', False)
        self.check_source('fn f(x:u32)->u32 {switch x {case 0u:{return 1u;} default:{}}}' +
                          ENTRY + '{let value=f(id.x);}', False)


if __name__ == '__main__':
    unittest.main()
