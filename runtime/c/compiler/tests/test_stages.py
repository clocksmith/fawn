#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Graphics interface source and actual SPIR-V developer checks, not GPU proof."""
from __future__ import annotations
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_cli import decode

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])
ROOT = Path(__file__).resolve().parents[4]


class Stages(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-stages-') as directory:
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

    def test_retained_vertex_sources(self):
        for relative in ['bench/kernels/draw_call_proxy_vertex.wgsl',
                         'bench/kernels/compilation-corpus/trivial_passthrough_vertex.wgsl']:
            code = self.compile((ROOT / relative).read_text())
            self.assertEqual([args[0] for op, args in code if op == 15], [0])
            self.assertFalse(any(op == 16 and args[1] == 17 for op, args in code))

    def test_mixed_entries_and_struct_values(self):
        code = self.compile('''
            struct Output { @builtin(position) position:vec4f,
                @location(7) @interpolate(flat) tag:u32, @location(2) color:vec3f }
            @vertex fn vertex(@location(1) point:vec4f,@builtin(instance_index) index:u32)->Output {
                return make(point,index); }
            fn make(p:vec4f,i:u32)->Output {return Output(p,i,vec3f(0.5));}
            @fragment fn fragment(input:Output)->@location(0) vec4f {return vec4f(input.color,1.0);}
            @compute @workgroup_size(2) fn compute(){}
        ''')
        self.assertEqual([args[0] for op, args in code if op == 15], [0, 4, 5])
        self.assertTrue(any(op == 16 and args[1] == 7 for op, args in code))
        self.assertEqual(sum(op == 16 and args[1] == 17 for op, args in code), 1)
        self.assertTrue(any(op == 62 for op, _ in code))
        self.assertTrue(any(op == 80 for op, _ in code))

    def test_fragment_builtins_and_sampling(self):
        code = self.compile('''
            struct Input { @builtin(front_facing) face:bool, @builtin(sample_index) index:u32,
                @builtin(sample_mask) mask:u32, @builtin(position) p:vec4f,
                @location(0) @interpolate(linear,sample) color:vec4f }
            struct Output { @builtin(frag_depth) depth:f32, @builtin(sample_mask) mask:u32,
                @location(0) color:vec4f }
            @fragment fn main(input:Input)->Output {
                if(input.face){return Output(input.p.z,input.mask,input.color);}
                return Output(0.5,input.index,vec4f(0.0)); }
        ''')
        self.assertIn(35, [args[0] for op, args in code if op == 17])
        self.assertTrue(any(op == 16 and args[1] == 12 for op, args in code))
        self.assertTrue(any(op == 28 for op, _ in code))
        self.assertTrue(any(op == 71 and args[1:] == (11, 20) for op, args in code))
        self.assertTrue(any(op == 71 and args[1] == 17 for op, args in code))

    def test_helper_signature_and_source_static_stage_closure(self):
        for bodies in [
            '@vertex fn vertex()->@builtin(position) vec4f{return make();} fn make()->vec4f{return vec4f(1.0);}',
            'fn make()->vec4f{return vec4f(1.0);} @vertex fn vertex()->@builtin(position) vec4f{return make();}'
        ]:
            self.compile(bodies)
        self.compile('''var<workgroup> group_value:u32; fn hidden()->bool{let x=group_value;return true;}
            @fragment fn main(){let skipped=false&&hidden();}''', 'restricted to compute')
        self.compile('''@group(0) @binding(0) var<storage,read_write> data:array<u32>;
            fn hidden()->bool{data[0]=1u;return true;}
            @vertex fn main()->@builtin(position) vec4f{let skipped=false&&hidden();return vec4f(1.0);}''',
            'writable storage resources')
        self.compile('''enable subgroups; fn hidden()->u32{return subgroupAdd(1u);}
            @vertex fn main()->@builtin(position) vec4f{return vec4f(1.0);let dead=hidden();}''',
            'not available in vertex')

    def test_interface_rejections_include_unused_siblings(self):
        valid = '@compute @workgroup_size(1) fn valid(){}'
        cases = [
            ('@vertex fn bad(){}', 'must return the position'),
            ('@vertex fn bad()->@builtin(position) vec3f{return vec3f(0.0);}', 'type is incorrect'),
            ('@fragment fn bad(@builtin(vertex_index) x:u32){}', 'stage and direction'),
            ('@fragment fn bad(@location(0) x:u32){}', 'explicit flat'),
            ('@fragment fn bad(@location(0) x:bool){}', 'must be numeric'),
            ('@fragment fn bad(@location(0) x:f32,@location(0) y:f32){}', 'duplicate builtin or location'),
            ('@fragment fn bad(@builtin(front_facing) x:bool,@builtin(front_facing) y:bool){}', 'duplicate builtin or location'),
            ('@fragment fn bad(@location(0) @interpolate(flat,center) x:f32){}', 'invalid interpolation'),
            ('@fragment fn bad()->@location(0) @interpolate(flat) u32{return 0u;}', 'interpolation requires'),
            ('@fragment fn bad(@location(0) @invariant x:f32){}', 'invariant requires'),
            ('struct A{@location(0) x:f32} @fragment fn bad(@location(1) x:A){}', 'scalar or vector leaf'),
            ('struct A{x:f32} @fragment fn bad(x:A){}', 'missing builtin or location'),
            ('@fragment fn bad(@location(0) x:f32){let x=1.0;}', 'duplicates a binding'),
            ('@fragment @workgroup_size(1) fn bad(){}', 'one stage attribute'),
        ]
        for source, error in cases:
            with self.subTest(source=source):
                self.compile(valid + source, error)

    def test_interfaces_exceed_per_instruction_temporary_allowance(self):
        fields = '@builtin(position) p:vec4f,' + ','.join(
            f'@location({i}) v{i}:f32' for i in range(80))
        values = 'vec4f(0.0,0.0,0.0,1.0),' + ','.join('1.0' for _ in range(80))
        code = self.compile('struct O{' + fields + '}' +
            '@vertex fn vertex()->O{return O(' + values + ');}' +
            '@fragment fn fragment(input:O)->@location(0) vec4f{return vec4f(input.v79);}')
        entry_records = [args for op, args in code if op == 15]
        self.assertEqual(len(entry_records), 2)
        self.assertGreater(sum(op == 62 for op, _ in code), 64)

    def test_interpolation_invariant_and_dynamic_locations(self):
        code = self.compile('''const LOCATION=2u;
            struct O{@builtin(position) @invariant p:vec4f,
                @location(LOCATION+1u) @interpolate(flat,either) id:u32,
                @location(0) @interpolate(perspective,centroid) color:vec4f}
            @vertex fn main(@builtin(vertex_index) index:u32)->O{return O(vec4f(1.0),index,vec4f(0.5));}
        ''')
        self.assertTrue(any(op == 71 and args[1:] == (30, 3) for op, args in code))
        self.assertTrue(any(op == 71 and args[1:] == (18,) for op, args in code))
        self.assertTrue(any(op == 71 and args[1:] == (16,) for op, args in code))


if __name__ == '__main__':
    unittest.main()
