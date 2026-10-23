#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Runtime-array length source/target checks, distinct from GPU execution."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_cli import decode

ROOT = Path(__file__).resolve().parents[4]
CLI = os.environ['DOE_COMPILER_CLI']
VALIDATOR = os.environ['SPIRV_VAL']
ENTRY = '@compute @workgroup_size(1) fn main()'
BUFFER = '@group(0) @binding(0) var<storage,read_write> data:array<u32>;'


class ArrayLength(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-array-length-') as directory:
            source_path = Path(directory) / 'case.wgsl'
            target = Path(directory) / 'case.spv'
            source_path.write_text(source)
            result = subprocess.run([CLI, '--shader-path', str(source_path), '--out', str(target)],
                                    capture_output=True, text=True)
            if error:
                self.assertNotEqual(result.returncode, 0, source)
                self.assertIn(error, result.stderr)
                self.assertFalse(target.exists())
                return None
            self.assertEqual(result.returncode, 0, result.stderr)
            validated = subprocess.run([VALIDATOR, '--target-env', 'vulkan1.1', str(target)],
                                       capture_output=True, text=True)
            self.assertEqual(validated.returncode, 0, validated.stderr)
            return decode(target.read_bytes())

    def test_retained_timestamp_normalizer(self):
        constants = ('const MANTISSA=10485760u; const PERIOD_SHIFT=-22i; '
                     'const MASK_LOW=4294967295u; const MASK_HIGH=65535u; '
                     'const WORKGROUP_SIZE=64u;\n')
        kernel = ROOT / 'runtime/zig/src/backend/vulkan/vk_timestamp_normalize.wgsl'
        code = self.compile(constants + kernel.read_text())
        self.assertTrue(any(op == 68 for op, _ in code))

    def test_typed_resource_and_member_identity(self):
        code = self.compile('''
            struct Tail { prefix:vec4u, other:u32, values:array<vec2u> }
            @group(0) @binding(4) var<storage,read> structured:Tail;
            @group(0) @binding(9) var<storage,read> direct:array<u32>;
            fn size()->u32{return arrayLength((&((structured).values)));}
            @compute @workgroup_size(1) fn main(){let x=size()+arrayLength(&direct);}
        ''')
        bindings = {args[0]: args[2] for op, args in code if op == 71 and args[1] == 33}
        lengths = [(bindings[args[2]], args[3]) for op, args in code if op == 68]
        self.assertCountEqual(lengths, [(4, 2), (9, 0)])

    def test_uniform_metadata_and_dead_source(self):
        self.compile(BUFFER + ENTRY + '{if(arrayLength(&data)>0u){workgroupBarrier();}}')
        self.compile(BUFFER + ENTRY + '''{
            let skipped=false&&(arrayLength(&data)>0u);
            if(skipped){workgroupBarrier();}
        }''')
        self.compile(BUFFER + ENTRY + '{return;let n=arrayLength(&absent);}', 'storage resource')
        self.compile(BUFFER + ENTRY + '{let n=false&&(arrayLength(&absent)>0u);}', 'storage resource')

    def test_invalid_shapes_and_namespace(self):
        cases = [
            ('arrayLength(data)', 'address'), ('arrayLength(&data[0])', 'runtime array'),
            ('arrayLength(&missing)', 'storage resource'), ('arrayLength()', 'one runtime'),
            ('arrayLength(&data,&data)', 'one runtime'), ('arrayLength<u32>(&data)', 'one runtime'),
        ]
        for expression, error in cases:
            self.compile(BUFFER + ENTRY + '{let n=' + expression + ';}', error)
        self.compile(BUFFER + ENTRY + '{arrayLength(&data);}', 'used result')
        self.compile(BUFFER + ENTRY + '{let p=&data;}', 'only as arrayLength')
        self.compile(BUFFER + ENTRY + '{let data=2u;let n=arrayLength(&data);}', 'storage resource')
        self.compile(BUFFER + ENTRY + '{let arrayLength=1u;let n=arrayLength(&data);}', 'lexical value')
        self.compile('@group(0) @binding(0) var<storage,read> a:array<u32,4>;' + ENTRY +
                     '{let n=arrayLength(&a);}', 'runtime-sized')
        self.compile('struct T{head:u32,tail:array<u32>} @group(0) @binding(0) '
                     'var<storage,read> a:T;' + ENTRY + '{let n=arrayLength(&a.head);}', 'runtime array tail')
        self.compile(BUFFER + 'const n=arrayLength(&data);' + ENTRY + '{}', 'constant')


if __name__ == '__main__':
    unittest.main()
