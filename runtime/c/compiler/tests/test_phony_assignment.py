#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Phony statements preserve RHS effects, validation and static interfaces."""
import unittest
import test_stages
from function_machine import execute_functions

ENTRY = '@compute @workgroup_size(1) fn main()'
DATA = '@group(0) @binding(0) var<storage,read_write> data:array<u32>;'


class PhonyAssignment(unittest.TestCase):
    compile = test_stages.Stages.compile

    def test_discarded_const_and_constructible_values(self):
        self.compile(ENTRY + '''{
            _ = 9223372036854775807; _ = 1e200; _ = vec3(1,2,3);
            _ = array(1,2,3); _ = mat2x2f(); _ = true;
            var x=3u; _ = x; _ = &x; _ = sin(0.0);
        }''')

    def test_must_use_results_and_once_only_calls(self):
        source = DATA + '''
            @must_use fn step()->u32{data[0]+=1u;return data[0];}
            fn first(x:u32,y:u32)->u32{data[1]=x*10u+y;return x;}
        ''' + ENTRY + '{_ = first(step(),step());_ = max(step(),step());}'
        code = self.compile(source)
        self.assertEqual(sum(op == 57 for op, _ in code), 5)
        memory = {(0, 0): [0, 0, 0x12345678]}
        execute_functions(code, memory, 0)
        self.assertEqual(memory[(0, 0)], [4, 12, 0x12345678])
        self.compile(DATA + '@must_use fn step()->u32{return data[0];}' +
                     ENTRY + '{step();}', 'result context')
        self.compile(ENTRY + '{sin(0.0);}', 'used result')

    def test_static_pointer_and_handle_resource_closure(self):
        code = self.compile('''
            struct Buffer { counter:atomic<u32>, data:array<vec4f> }
            @group(0) @binding(1) var<storage,read_write> buf:Buffer;
            @group(0) @binding(2) var tex:texture_2d<f32>;
            @group(0) @binding(3) var smp:sampler;
            @group(0) @binding(4) var<storage,read> unused:array<u32>;
            @fragment fn shade()->@location(0) vec4f{
                _ = &buf; _ = tex; _ = smp; return vec4f();
            }
        ''')
        bindings = {a[2] for op, a in code if op == 71 and a[1] == 33}
        # SPIR-V retains whole-module declarations. The public text API test
        # separately checks the emitted entry closure excludes binding 4.
        self.assertEqual(bindings, {1, 2, 3, 4})

    def test_no_assignment_target_or_void_value(self):
        for left in ['_ += 1u', '_++', '_[0] = 1u', '_.x = 1u']:
            self.compile(ENTRY + '{' + left + ';}', 'phony assignment token')
        self.compile(ENTRY + '{let _=1u;}', 'identifier')
        self.compile(ENTRY + '{let x=_;}', 'name does not resolve')
        self.compile('fn no_result(){}' + ENTRY + '{_ = no_result();}', 'result context')
        self.compile(ENTRY + '{_ = workgroupBarrier();}', 'no result')
        self.compile(DATA + ENTRY + '{_ = data;}', 'not a value')

    def test_dead_invalid_rhs_is_still_validated(self):
        for body, diagnostic in [('_ = missing;', 'name does not resolve'),
                                 ('_ = 1u/0u;', 'zero const expression'),
                                 ('_ = 9223372036854775807+1;', 'overflow')]:
            self.compile(ENTRY + '{return;' + body + '}', diagnostic)

    def test_short_circuit_and_uniformity_remain_source_based(self):
        code = self.compile(DATA + 'fn step()->bool{data[0]+=1u;return true;}' +
                            ENTRY + '{_ = false && step();_ = true || step();}')
        self.assertFalse(any(op == 57 for op, _ in code))
        memory = {(0, 0): [17]}
        execute_functions(code, memory, 0)
        self.assertEqual(memory[(0, 0)], [17])
        self.compile('fn sync()->bool{workgroupBarrier();return true;}' +
                     '@compute @workgroup_size(2) fn main(@builtin(local_invocation_index) i:u32)'
                     '{if(i==0u){_ = false && sync();}}', 'uniform')

    def test_discarded_address_evaluates_indices_without_loading(self):
        code = self.compile(DATA + 'fn step()->u32{data[0]+=1u;return 999999u;}' +
                            ENTRY + '{_ = &data[step()];}')
        self.assertEqual(sum(op == 57 for op, _ in code), 1)
        # One length query belongs to the helper's load, one to its store.
        self.assertEqual(sum(op == 68 for op, _ in code), 2)
        memory = {(0, 0): [0]}
        execute_functions(code, memory, 0)
        self.assertEqual(memory[(0, 0)], [1])
        self.compile(ENTRY + '{var v=vec2u();_ = &v.x;}', 'not addressable')

    def test_loop_initializer_update_and_continue(self):
        self.compile(DATA + ENTRY + '''{
            var i=0u;
            for(_ = data[i];i<3u;_ = data[i]){i++;continue;}
            loop {i++;continuing{_ = data[i];break if i>4u;}}
        }''')

    def test_unchanged_retained_stress_source(self):
        path = test_stages.ROOT / 'bench/kernels/shader_compile_pipeline_stress.wgsl'
        code = self.compile(path.read_text())
        self.assertTrue(any(op == 12 and a[3] == 13 for op, a in code))
        self.assertTrue(any(op == 246 for op, _ in code))


if __name__ == '__main__':
    unittest.main()
