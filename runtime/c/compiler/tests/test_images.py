#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Resource-family developer tests: typed WGSL and actual SPIR-V, no GPU claims."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_cli import decode

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])
ENTRY = '@compute @workgroup_size(1) fn main()'
FORMATS = {
    'rgba32float': (1, 'f32'), 'rgba16float': (2, 'f32'), 'r32float': (3, 'f32'),
    'rgba8unorm': (4, 'f32'), 'rgba8snorm': (5, 'f32'), 'rg32float': (6, 'f32'),
    'rgba32sint': (21, 'i32'), 'rgba16sint': (22, 'i32'), 'rgba8sint': (23, 'i32'),
    'r32sint': (24, 'i32'), 'rg32sint': (25, 'i32'), 'rgba32uint': (30, 'u32'),
    'rgba16uint': (31, 'u32'), 'rgba8uint': (32, 'u32'), 'r32uint': (33, 'u32'),
    'rg32uint': (35, 'u32'),
}


def texture(name, type_name, binding=0):
    return f'@group(0) @binding({binding}) var {name}:{type_name};'


class Images(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-images-') as directory:
            shader, target = Path(directory) / 'case.wgsl', Path(directory) / 'case.spv'
            shader.write_text(source)
            result = subprocess.run([str(CLI), '--shader-path', str(shader), '--out', str(target)],
                                    capture_output=True, text=True)
            if error is not None:
                self.assertNotEqual(result.returncode, 0, source)
                self.assertIn(error, result.stderr)
                self.assertFalse(target.exists())
                return None
            self.assertEqual(result.returncode, 0, (source, result.stderr))
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(target)],
                                   capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, (source, valid.stderr))
            return decode(target.read_bytes())

    def test_storage_formats_dimensions_and_access(self):
        for dimension, coordinate, layer in [('1d', '0i', ''), ('2d', 'vec2i(0)', ''),
                                             ('2d_array', 'vec2u(0)', ',1i'), ('3d', 'vec3u(0)', '')]:
            for name, (format_code, scalar) in FORMATS.items():
                with self.subTest(dimension=dimension, format=name):
                    declaration = texture('t', f'texture_storage_{dimension}<{name},read_write>')
                    code = self.compile(declaration + ENTRY + '{let x=textureLoad(t,' + coordinate + layer +
                        ');textureStore(t,' + coordinate + layer + f',vec4<{scalar}>(1));let n=textureDimensions(t);' + '}')
                    image = [args for op, args in code if op == 25]
                    self.assertEqual(len(image), 1)
                    self.assertEqual(image[0][-1], format_code)
                    self.assertTrue(any(op == 98 for op, _ in code))
                    self.assertTrue(any(op == 99 for op, _ in code))
                    capabilities = [args[0] for op, args in code if op == 17]
                    self.assertIn(50, capabilities)
                    self.assertEqual(49 in capabilities, name.startswith('rg32'))
                    self.assertNotIn(55, capabilities)
                    self.assertNotIn(56, capabilities)

    def test_sampled_dimensions_types_and_sampling(self):
        for dimension, coordinate, layer in [('1d', '-1i', ''), ('2d', 'vec2i(-1,0)', ''),
                                             ('2d_array', 'vec2u(0)', ',1i'), ('3d', 'vec3u(0)', '')]:
            for scalar in ('f32', 'i32', 'u32'):
                self.compile(texture('t', f'texture_{dimension}<{scalar}>') + ENTRY +
                    '{let x=textureLoad(t,' + coordinate + layer + ',0i);let n=textureDimensions(t,0u);let l=textureNumLevels(t);}')
        for dimension, coordinate, layer in [('1d', '0.5', ''), ('2d', 'vec2f(0.5)', ''),
                ('2d_array', 'vec2f(0.5)', ',-1'), ('3d', 'vec3f(0.5)', ''),
                ('cube', 'vec3f(0.5)', ''), ('cube_array', 'vec3f(0.5)', ',3u')]:
            code = self.compile(texture('t', f'texture_{dimension}<f32>') + texture('s', 'sampler', 1) + ENTRY +
                '{let x=textureSampleLevel(t,s,' + coordinate + layer + ',0.0);let n=textureDimensions(t);}')
            self.assertTrue(any(op == 88 and args[-2] == 2 for op, args in code))
            self.assertFalse(any(op == 87 for op, _ in code))
            if layer:
                self.assertTrue(any(op == 112 for op, _ in code))
                self.assertTrue(any(op == 169 for op, _ in code))

    def test_depth_multisample_and_canonical_targets(self):
        for name, layer, multi in [('2d', '', False), ('2d_array', ',1', False), ('multisampled_2d', '', True)]:
            self.compile(texture('t', 'texture_depth_' + name) + ENTRY +
                '{let x:f32=textureLoad(t,vec2i(0)' + layer + ',0);let n=textureDimensions(t);' +
                ('let s=textureNumSamples(t);' if multi else 'let l=textureNumLevels(t);') + '}')
        for scalar in ('f32', 'i32', 'u32'):
            self.compile(texture('t', f'texture_multisampled_2d<{scalar}>') + ENTRY +
                '{let x=textureLoad(t,vec2i(-1),-1);let n=textureNumSamples(t);}')
        code = self.compile(texture('s', 'sampler') + texture('c', 'sampler_comparison', 1) + ENTRY + '{}')
        self.assertEqual(sum(op == 26 for op, _ in code), 1)
        code = self.compile(texture('a', 'texture_storage_2d<rgba8unorm,read>') +
            texture('b', 'texture_storage_2d<rgba8unorm,write>', 1) + ENTRY +
            '{let x=textureLoad(a,vec2i(0));textureStore(b,vec2i(0),x);}')
        self.assertEqual(sum(op == 25 for op, _ in code), 1)

    def test_guard_order_and_single_argument_evaluation(self):
        declarations = texture('t', 'texture_2d<f32>') + \
            '@group(0) @binding(1) var<storage,read_write> calls:array<i32>;'
        helper = 'fn level()->i32{calls[0]+=1;return calls[0];}'
        code = self.compile(declarations + helper + ENTRY + '{let x=textureLoad(t,vec2i(-1),level());}')
        self.assertEqual(sum(op == 57 for op, _ in code), 1)
        selects = {args[1]: index for index, (op, args) in enumerate(code) if op == 169}
        queries = [(index, args) for index, (op, args) in enumerate(code) if op == 103]
        self.assertEqual(len(queries), 1)
        index, args = queries[0]
        self.assertIn(args[-1], selects)
        self.assertLess(selects[args[-1]], index)
        fetch = next(index for index, (op, _) in enumerate(code) if op == 95)
        self.assertTrue(any(op == 250 for op, _ in code[index:fetch]))
        self.assertTrue(any(op == 175 for op, _ in code[:fetch]))
        self.assertTrue(any(op == 176 for op, _ in code[:fetch]))
        self.assertTrue(any(op == 245 for op, _ in code[fetch:]))

    def test_semantics_names_dead_code_and_uniformity(self):
        sampled = texture('t', 'texture_2d<f32>')
        read = texture('t', 'texture_storage_2d<rgba8unorm,read>')
        write = texture('t', 'texture_storage_2d<rgba8unorm,write>')
        cases = [
            (sampled, 'textureLoad(t,vec2i(0),0);', 'result context'),
            (sampled, 'let x=textureLoad(t,vec2f(0),0);', 'implicit conversion'),
            (sampled, 'let x=textureLoad(t,vec2i(0));', 'no matching'),
            (read, 'textureStore(t,vec2i(0),vec4f(0));', 'no matching'),
            (write, 'let x=textureLoad(t,vec2i(0));', 'no matching'),
            (sampled, 'let t=1;let x=textureLoad(t,vec2i(0),0);', 'texture handle'),
            (sampled, 'let textureLoad=1;let x=textureLoad(t,vec2i(0),0);', 'lexical value'),
            (sampled, 'return;let x=textureLoad(t,vec2f(0),0);', 'implicit conversion'),
            (sampled, 'let x=false&&(textureLoad(t,vec2f(0),0).x>0.0);', 'implicit conversion'),
            (sampled, 'var x:texture_2d<f32>;', 'constructible'),
        ]
        for declaration, body, error in cases:
            with self.subTest(body=body):
                self.compile(declaration + ENTRY + '{' + body + '}', error)
        self.compile('struct Bad { image:texture_2d<f32>, }' + ENTRY + '{}', 'member type')
        self.compile('@group(0) @binding(0) var<storage> t:texture_2d<f32>;' + ENTRY + '{}', 'handle address space')
        self.compile(texture('t', 'texture_storage_2d<bgra8unorm,write>') + ENTRY + '{}', 'unsupported storage texture format')
        for access, error in [('read', None), ('read_write', 'uniform')]:
            self.compile(texture('t', f'texture_storage_2d<rgba8unorm,{access}>') + ENTRY +
                '{if(textureLoad(t,vec2i(0)).x>0.0){workgroupBarrier();}}', error)
        helper = 'fn load(x:texture_2d<f32>)->vec4f{return textureLoad(x,vec2i(0),0);}'
        self.compile(sampled + helper + ENTRY + '{let x=load(t);}')

    def test_retained_image_shader(self):
        root = Path(__file__).resolve().parents[4]
        source = (root / 'bench/kernels/texture_sample_to_storage_64.wgsl').read_text()
        code = self.compile(source)
        self.assertTrue(any(op == 95 for op, _ in code))
        self.assertTrue(any(op == 99 for op, _ in code))


if __name__ == '__main__':
    unittest.main()
