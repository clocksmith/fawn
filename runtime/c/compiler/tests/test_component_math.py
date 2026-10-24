#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Shared component math: const values, runtime SPIR-V and retained sources."""
import unittest
import math
import struct
import test_stages

FLOAT_OPS = {'floor': 8, 'round': 2, 'trunc': 3, 'fract': 10, 'sin': 13,
             'cos': 14, 'tan': 15, 'asin': 16, 'acos': 17, 'atan': 18,
             'sinh': 19, 'cosh': 20, 'asinh': 22, 'acosh': 23, 'atanh': 24,
             'log': 28, 'log2': 30, 'exp2': 29, 'inverseSqrt': 32,
             'degrees': 12, 'radians': 11}
ENTRY = '@compute @workgroup_size(1) fn main()'
BUFFER = '@group(0) @binding(0) var<storage,read_write> data:array<vec4f>;'


class ComponentMath(unittest.TestCase):
    compile = test_stages.Stages.compile

    def vector_constant(self, expression, expected):
        code = self.compile('@fragment fn main()->@location(0) vec4f {return '+expression+';}')
        scalars = {a[1]: a[2] for op, a in code if op == 43}
        vectors = [tuple(scalars.get(x) for x in a[2:]) for op, a in code if op == 44]
        self.assertIn(tuple(expected), vectors)
        self.assertFalse(any(op == 12 for op, _ in code))

    def test_exact_const_rounding_sign_and_integer_minimum(self):
        self.vector_constant('round(vec4f(0.5,1.5,2.5,-1.5))', [0,0x40000000,0x40000000,0xc0000000])
        self.vector_constant('abs(vec4f(-0.0,-1.0,2.0,-3.0))', [0,0x3f800000,0x40000000,0x40400000])
        self.vector_constant('sign(vec4f(-0.0,-1.0,2.0,0.0))', [0,0xbf800000,0x3f800000,0])
        self.vector_constant('fract(vec4f(-0.25,0.25,-1.0,-0x1p-149f))',
                             [0x3f400000,0x3e800000,0,0x3f800000])
        self.vector_constant('vec4f(f32(abs(bitcast<i32>(2147483648u))),f32(sign(abs(-9223372036854775807-1))),f32(sign(-9)),f32(abs(9u)))',
                             [0xcf000000,0xbf800000,0xbf800000,0x41100000])
        self.vector_constant('vec4f(floor(-1.25),trunc(-1.25),exp2(3),inverseSqrt(4))',
                             [0xc0000000,0xbf800000,0x41000000,0x3f000000])
        self.vector_constant('vec4f(log(1),log2(8),cos(0),sin(0))', [0,0x40400000,0x3f800000,0])
        # AbstractFloat must not be prematurely narrowed before cancellation.
        self.vector_constant('vec4f(floor(16777217.25)-16777216.0)', [0x3f800000]*4)

    def test_reflect_constant_roles_shapes_and_no_normalization(self):
        self.vector_constant('reflect(vec4f(1,-2,3,-4),vec4f(0,1,0,0))',
                             [0x3f800000,0x40000000,0x40400000,0xc0800000])
        self.vector_constant('vec4f(reflect(vec3(1,-2,3),vec3(0,2,0)),1)',
                             [0x3f800000,0x41600000,0x40400000,0x3f800000])
        self.vector_constant('vec4f(reflect(vec3f(0,1,0),vec3f(1,-2,3)),1)',
                             [0x40800000,0xc0e00000,0x41400000,0x3f800000])
        self.vector_constant('vec4f(reflect(vec2(1,-2),vec2(0)),0,1)',
                             [0x3f800000,0xc0000000,0,0x3f800000])

    def test_reflect_runtime_overloads_order_and_shadowing(self):
        for width in range(2,5):
            typ = f'vec{width}f'
            code = self.compile(f'@group(0) @binding(0) var<storage,read_write> data:array<{typ}>;'+
                                ENTRY+'{data[2]=reflect(data[0],data[1]);}')
            self.assertEqual(sum(op == 12 and a[3] == 71 for op,a in code),1)
        code = self.compile(BUFFER+'fn read()->vec4f{data[0]=data[0]+vec4f(1);return data[0];}'+
                            ENTRY+'{data[1]=reflect(read(),read());}')
        calls = [a[1] for op,a in code if op == 57]
        reflected = [a[4:6] for op,a in code if op == 12 and a[3] == 71]
        self.assertEqual(len(calls),2)
        self.assertEqual(reflected,[tuple(calls)])
        for expression in ['reflect(1.0,2.0)','reflect(vec2i(1),vec2i(2))',
                           'reflect(vec2f(1),vec3f(1))','reflect(vec2f(1))']:
            self.compile(ENTRY+'{return;let invalid='+expression+';}', 'builtin')
        code = self.compile('fn reflect(a:u32,b:u32)->u32{return a+b;}'+ENTRY+'{let a=reflect(2u,3u);}')
        self.assertEqual(sum(op == 57 for op,_ in code),1)
        self.assertFalse(any(op == 12 and a[3] == 71 for op,a in code))

    def test_all_float_shapes_and_stage_split(self):
        for name, instruction in FLOAT_OPS.items():
            for width in range(1,5):
                typ = 'f32' if width == 1 else f'vec{width}f'
                source = f'@group(0) @binding(0) var<storage,read_write> data:array<{typ}>;'
                code = self.compile(source+ENTRY+'{let x=data[0];data[1]='+name+'(x);}')
                self.assertEqual(sum(op == 12 and a[3] == instruction for op,a in code), 1)
            value = '1.0' if name in ('acosh','log','log2','inverseSqrt') else '0.0'
            code = self.compile(ENTRY+'{const result='+name+'('+value+');let value=result;}')
            self.assertFalse(any(op == 12 for op,_ in code))
        for name,leaf,instruction in [('abs','f32',4),('abs','i32',5),('sign','f32',6),('sign','i32',7)]:
            code = self.compile(f'@group(0) @binding(0) var<storage,read_write> data:array<vec4<{leaf}>>;'+
                                ENTRY+'{data[1]='+name+'(data[0]);}')
            self.assertTrue(any(op == 12 and a[3] == instruction for op,a in code))
        code = self.compile('@group(0) @binding(0) var<storage,read_write> data:array<u32>;'+ENTRY+'{data[1]=abs(data[0]);}')
        self.assertFalse(any(op == 11 for op,_ in code))

    def test_domains_skipping_dead_source_and_overload_rejection(self):
        for expression in ['asin(2.0)','acos(-2.0)','acosh(0.5)','atanh(2.0)','log(-1.0)',
                           'log2(0.0)','inverseSqrt(-1.0)']:
            self.compile('const bad='+expression+';'+ENTRY+'{}', 'outside its domain')
            self.compile(ENTRY+'{let skipped=false && ('+expression+'==0.0);}')
        for expression in ['atanh(1.0)','inverseSqrt(0.0)','exp2(1024.0)','cosh(1000.0)']:
            self.compile('const bad='+expression+';'+ENTRY+'{}', 'not finite')
        for expression in ['sign(1u)','abs(true)','sin(1i)','floor(vec2u(1))','log(mat2x2f())']:
            self.compile(ENTRY+'{return;let invalid='+expression+';}', 'builtin')
        self.compile(ENTRY+'{let skipped=false && (sin(missing)==0.0);}', 'name does not resolve')
        self.compile(ENTRY+'{sin(0.0);}', 'used result')
        self.compile('fn sin(x:u32)->u32{return x+1u;}'+ENTRY+'{let x=sin(4u);}')

    def test_single_effectful_argument(self):
        code = self.compile(BUFFER+'fn read()->vec4f{data[0]=data[0]+vec4f(1);return data[0];}'+
                            ENTRY+'{data[1]=fract(read());data[2]=abs(read());}')
        self.assertEqual(sum(op == 57 for op,_ in code), 2)

    def test_two_argument_float_shapes_and_constants(self):
        for name, instruction in [('atan2', 25), ('pow', 26)]:
            for width in range(1, 5):
                typ = 'f32' if width == 1 else f'vec{width}f'
                source = f'@group(0) @binding(0) var<storage,read_write> data:array<{typ}>;'
                code = self.compile(source + ENTRY + '{data[2]=' + name + '(data[0],data[1]);}')
                self.assertEqual(sum(op == 12 and a[3] == instruction for op, a in code), 1)
        self.vector_constant('pow(vec4f(2,4,9,16),vec4f(3,0.5,0.5,-0.5))',
                             [0x41000000, 0x40000000, 0x40400000, 0x3e800000])
        self.vector_constant('vec4f(pow(16777217.0,1.0)-16777216.0)', [0x3f800000] * 4)
        code = self.compile('@fragment fn main()->@location(0) vec4f {'
                            'return atan2(vec4f(1,1,-1,-1),vec4f(1,-1,-1,1));}')
        scalars = {a[1]: a[2] for op, a in code if op == 43}
        vectors = [a[2:] for op, a in code if op == 44 and all(x in scalars for x in a[2:])]
        actual = [struct.unpack('<f', struct.pack('<I', scalars[x]))[0] for x in vectors[-1]]
        for value, expected in zip(actual, [math.pi/4, 3*math.pi/4, -3*math.pi/4, -math.pi/4]):
            self.assertAlmostEqual(value, expected, delta=3e-7)
        self.assertFalse(any(op == 12 for op, _ in code))

    def test_two_argument_domains_order_and_resolution(self):
        self.compile('const invalid=pow(-2.0,2.0);' + ENTRY + '{}', 'outside its domain')
        self.compile(ENTRY + '{let skipped=false && (pow(-2.0,2.0)==4.0);}')
        for expression in ['pow(0.0,-1.0)', 'pow(1e38f,2.0f)']:
            self.compile('const invalid=' + expression + ';' + ENTRY + '{}', 'not finite')
        for expression in ['pow(2i,3i)', 'atan2(1u,2u)', 'pow(2.0)',
                           'atan2(1.0,2.0,3.0)']:
            self.compile(ENTRY + '{return;let invalid=' + expression + ';}', 'builtin')
        self.compile(ENTRY + '{let invalid=pow(vec2f(1),1.0);}', 'matching scalar or vector shapes')
        code = self.compile(BUFFER + 'fn read()->vec4f{data[0]=data[0]+vec4f(1);return data[0];}' +
                            ENTRY + '{data[1]=pow(read(),read());data[2]=atan2(read(),read());}')
        calls = [a[1] for op, a in code if op == 57]
        arguments = [a[4:6] for op, a in code if op == 12 and a[3] in (25, 26)]
        self.assertEqual(len(calls), 4)
        self.assertEqual(arguments, [tuple(calls[:2]), tuple(calls[2:])])
        code = self.compile('fn pow(a:u32,b:u32)->u32{return a+b;} fn atan2(a:u32,b:u32)->u32{return a-b;}' +
                            ENTRY + '{let a=pow(1u,2u);let b=atan2(4u,1u);}')
        self.assertEqual(sum(op == 57 for op, _ in code), 2)
        self.assertFalse(any(op == 12 and a[3] in (25, 26) for op, a in code))

    def test_retained_fluid_sources(self):
        for name in ['stable_fluids_advect_velocity.wgsl','stable_fluids_advect_dye.wgsl']:
            code = self.compile((test_stages.ROOT/'bench/kernels'/name).read_text())
            self.assertTrue(any(op == 12 and a[3] == 8 for op,a in code))


if __name__ == '__main__':
    unittest.main()
