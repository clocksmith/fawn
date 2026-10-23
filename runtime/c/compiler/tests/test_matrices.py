#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Matrix source/word checks. These are not physical GPU qualification."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from test_cli import decode

CLI = os.environ['DOE_COMPILER_CLI']
VALIDATOR = os.environ['SPIRV_VAL']
ENTRY = '@compute @workgroup_size(1) fn main()'
OUTPUT = '@group(0) @binding(9) var<storage,read_write> data:array<f32>;'


class Matrices(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-matrix-') as directory:
            path, target = Path(directory) / 'case.wgsl', Path(directory) / 'case.spv'
            path.write_text(source)
            result = subprocess.run([CLI, '--shader-path', str(path), '--out', str(target)],
                                    capture_output=True, text=True)
            if error:
                self.assertNotEqual(result.returncode, 0, source)
                self.assertIn(error, result.stderr)
                self.assertFalse(target.exists())
                return None
            self.assertEqual(result.returncode, 0, result.stderr)
            valid = subprocess.run([VALIDATOR, '--target-env', 'vulkan1.1', str(target)],
                                   capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            return decode(target.read_bytes())

    def test_all_shapes_and_runtime_operators(self):
        for columns in range(2, 5):
            for rows in range(2, 5):
                with self.subTest(columns=columns, rows=rows):
                    scalar = ','.join('x' for _ in range(columns * rows))
                    vectors = ','.join(f'vec{rows}f(x)' for _ in range(columns))
                    code = self.compile(ENTRY + f'''{{
                        let x=2.0; let a=mat{columns}x{rows}f({scalar});
                        let b=mat{columns}x{rows}<f32>({vectors});
                        let sum=a+b-a; let left=2.0*a; let right=a*2.0;
                        let mv=a*vec{columns}f(x); let vm=vec{rows}f(x)*a;
                        let transposed=transpose(a); let product=a*transposed;
                        let copied=mat{columns}x{rows}f(a);
                    }}''')
                    opcodes = {op for op, _ in code}
                    self.assertTrue({24, 84, 143, 144, 145, 146}.issubset(opcodes))
                    self.assertTrue(any(op == 24 and args[2] == columns for op, args in code))
        for size in range(2, 5):
            code = self.compile(ENTRY + f'{{let m=mat{size}x{size}f();let d=determinant(m);}}')
            self.assertTrue(any(op == 12 and args[3] == 33 for op, args in code))

    def test_constant_values_column_order_and_rectangular_product(self):
        expressions = [('M[1][0]', 3.0), ('transpose(M)[0][1]', 3.0),
                       ('determinant(M)', -2.0), ('(M*M)[0][0]', 7.0),
                       ('(M*vec2(2,3))[1]', 16.0), ('(vec2(2,3)*M)[1]', 18.0),
                       ('(2*M+M)[1][1]', 12.0), ('mat2x2(M)[0][1]', 2.0),
                       ('mat2x2()[1][0]', 0.0),
                       ('(mat2x3(1,2,3,4,5,6)*mat4x2(1,2,3,4,5,6,7,8))[3][2]', 69.0),
                       ('determinant(mat3x3(1,2,3,4,5,6,7,8,10))', -3.0),
                       ('determinant(mat4x4(1,0,0,0,0,2,0,0,0,0,3,0,0,0,0,4))', 24.0)]
        for expression, expected in expressions:
            with self.subTest(expression=expression):
                code = self.compile('const M=mat2x2(1,2,3,4);const answer=' + expression + ';' +
                                    OUTPUT + ENTRY + '{data[0]=answer;}')
                constants = {args[1]: args[2] for op, args in code if op == 43 and len(args) == 3}
                stores = [args[1] for op, args in code if op == 62]
                self.assertEqual(len(stores), 1)
                expected_bits = struct.unpack('<I', struct.pack('<f', expected))[0]
                self.assertEqual(constants[stores[0]], expected_bits)

    def test_uniform_storage_and_workgroup_layout(self):
        for columns in range(2, 5):
            for rows in range(2, 5):
                source = f'@group(0) @binding(0) var<uniform> m:mat{columns}x{rows}f;'
                self.compile(source + ENTRY + '{let whole=m;let i=1i;let x=m[i][i];let negative=-1i;let oob=m[negative][i];}')
                self.compile(source.replace('uniform', 'storage,read_write') + ENTRY +
                             '{let whole=m;let i=1i;m[i][i]=whole[i][i];}')
        self.compile('''
            struct Inner { matrix:mat2x2f, suffix:vec2u }
            struct Outer { prefix:vec2f, matrix:mat3x2f, @size(32) inner:Inner, items:array<mat2x2f,2> }
            @group(0) @binding(0) var<uniform> input:Outer;
            @group(0) @binding(1) var<storage,read_write> output:Outer;
            @compute @workgroup_size(1) fn main(){
                let a=input; output=a; let i=1i;
                let b=input.matrix[i][i];let c=input.items[i][i][i];
                if(i>0){let d=input.inner.suffix[i];}
            }
        ''')
        self.compile('var<workgroup> m:mat3x2f;' + ENTRY + '{let i=1u;m[i][i]=1.0;workgroupBarrier();}')

    def test_namespace_helpers_zero_and_static_validation(self):
        self.compile('''
            fn helper(a:mat3x2f,b:mat2x3f)->mat2x2f{return a*b;}
            @compute @workgroup_size(1) fn main(){
                var a:mat3x2f; let b=mat2x3f();let c=helper(a,b);
                let inferred=mat2x2(vec2(1,2),vec2(3,4));
            }
        ''')
        errors = [('mat2x3f(1)', 'identical dimensions'),
                  ('mat2x2f(vec2f(),1,2)', 'whole floating-point'),
                  ('mat2x2f(vec2i(),vec2i())', 'whole floating-point'),
                  ('mat2x2<u32>()', 'floating-point'), ('determinant(mat3x2f())', 'square'),
                  ('transpose(2.0)', 'matrix argument'), ('mat2x2f()*vec3f()', 'matrix dimensions'),
                  ('mat2x3f()+mat3x2f()', 'matrix dimensions'), ('-mat2x2f()', 'unary'),
                  ('mat2x2f()/2.0', 'matrix dimensions')]
        for expression, error in errors:
            with self.subTest(expression=expression):
                self.compile(ENTRY + '{return;let invalid=' + expression + ';}', error)
        self.compile(ENTRY + '{let mat2x2f=1u;let bad=mat2x2f();}', 'lexical value')
        self.compile('const bad=false&&(determinant(mat3x2f())>0.0);' + ENTRY + '{}', 'square')
        self.compile('const bad=mat2x2(1e308,0,0,1)*1e308;' + ENTRY + '{}', 'finite')


if __name__ == '__main__':
    unittest.main()
