#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Scalar atomics, shared robust addresses, source closure and target words."""
import unittest
import test_stages

ENTRY = '@compute @workgroup_size(1) fn main()'
ATOMIC = '@group(0) @binding(0) var<storage,read_write> a:array<atomic<u32>>;'


class Atomics(unittest.TestCase):
    compile = test_stages.Stages.compile

    def test_all_scalar_operations_and_scope_order(self):
        for scalar in ['i32', 'u32']:
            code = self.compile(f'''
                @group(0) @binding(0) var<storage,read_write> a:array<atomic<{scalar}>>;
                var<workgroup> cell:atomic<{scalar}>;
                @compute @workgroup_size(1) fn main() {{
                    let i=0; atomicStore(&a[i],1);let l=atomicLoad(&a[i]);
                    atomicAdd(&a[i],2);atomicSub(&a[i],3);
                    atomicMax(&a[i],4);atomicMin(&a[i],5);
                    atomicAnd(&a[i],6);atomicOr(&a[i],7);atomicXor(&a[i],8);
                    atomicExchange(&a[i],9);
                    let result=atomicCompareExchangeWeak(&a[i],9,10);
                    let old=result.old_value;let exchanged=result.exchanged;
                    atomicStore(&cell,old);let other=atomicLoad(&cell);
                }}''')
            opcodes = {op for op, _ in code}
            self.assertTrue({227, 228, 229, 230, 234, 235, 240, 241, 242}.issubset(opcodes))
            self.assertTrue(({236, 238} if scalar == 'i32' else {237, 239}).issubset(opcodes))
            self.assertNotIn(231, opcodes)  # Strong exchange is an allowed weak realization.
            constants = {args[1]: args[2] for op, args in code if op == 43}
            scopes = set()
            for op, args in code:
                if 227 <= op <= 242:
                    scope_index = 1 if op == 228 else 3
                    scopes.add(constants[args[scope_index]])
                    self.assertEqual(constants[args[scope_index + 1]], 0)
                    if op == 230:
                        self.assertEqual(constants[args[scope_index + 2]], 0)
            self.assertEqual(scopes, {1, 2})
            self.assertIn(224, opcodes)  # Existing workgroup zero initialization barrier.

    def test_nested_addresses_and_per_access_robustness(self):
        code = self.compile('''
            struct Row { prefix:u32, values:array<atomic<u32>,2> }
            struct Buffer { prefix:vec4u, rows:array<Row> }
            @group(0) @binding(0) var<storage,read_write> data:Buffer;
            @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){
                let outer=i32(id.x)-1;let inner=i32(id.y)-1;
                let a=atomicAdd(&data.rows[outer].values[inner],5u);
                let b=atomicCompareExchangeWeak(&data.rows[outer].values[inner],0u,9u);
                let zero=b.old_value;let no=b.exchanged;
                atomicStore(&data.rows[id.x].values[id.y],a);
            }''')
        # Storage atomics clamp (compiler/src/spirv_clamp.c): one OpArrayLength
        # per access for its runtime rows step, two OpSelects for that step's
        # last index and clamp plus one for the fixed values step, and no guard
        # branch, zero Phi or OOB zero struct.
        self.assertEqual(sum(op == 68 for op, _ in code), 3)
        self.assertEqual(sum(op == 169 for op, _ in code), 9)
        self.assertEqual(sum(op == 247 for op, _ in code), 0)
        self.assertEqual(sum(op == 250 for op, _ in code), 0)
        self.assertEqual(sum(op == 245 for op, _ in code), 0)
        self.assertFalse(any(op == 46 for op, _ in code))

    def test_atomic_containing_types_are_not_by_value(self):
        for declaration in ['var<workgroup> a:atomic<u32>;',
                            'struct S{a:atomic<u32>}var<workgroup> a:S;',
                            'var<workgroup> a:array<atomic<u32>,2>;']:
            self.compile(declaration+ENTRY+'{let copy=a;}', 'non-constructible')
            self.compile(declaration+ENTRY+'{a=a;}', 'non-constructible')
        for scalar in ['f32', 'bool', 'vec2u']:
            self.compile(f'var<workgroup> a:atomic<{scalar}>;'+ENTRY+'{}', 'i32 or u32')
        self.compile('@group(0) @binding(0) var<storage,read> a:atomic<u32>;'+ENTRY+'{}',
                     'read_write')
        self.compile('@group(0) @binding(0) var<uniform> a:atomic<u32>;'+ENTRY+'{}', 'read_write')
        self.compile(ENTRY+'{var local:atomic<u32>;}', 'constructible')
        self.compile('fn helper(x:atomic<u32>){}'+ENTRY+'{}', 'formal type')
        self.compile(ENTRY+'{let x=atomic<u32>();}', 'constructible')

    def test_overloads_namespace_and_nonconstant_calls(self):
        for expression in ['atomicLoad(a)', 'atomicLoad(&a)', 'atomicLoad(&a[0],1u)',
                           'atomicAdd(&a[0],1i)', 'atomicCompareExchangeWeak(&a[0],0u)',
                           'atomicLoad(&a[0]).unknown']:
            with self.subTest(expression=expression):
                self.compile(ATOMIC+ENTRY+'{return;let invalid='+expression+';}', '')
        self.compile(ATOMIC+ENTRY+'{let no=atomicStore(&a[0],1u);}', 'no result')
        self.compile(ATOMIC+'const no=atomicLoad(&a[0]);'+ENTRY+'{}', 'runtime function')
        self.compile('fn atomicAdd(x:u32,y:u32)->u32{return x+y;}'+ENTRY+'{let x=atomicAdd(1u,2u);}')
        self.compile(ATOMIC+ENTRY+'{let atomicLoad=1u;let x=atomicLoad(&a[0]);}', 'lexical value')
        self.compile('var<workgroup> a:array<atomic<u32>,2>;'+ENTRY+'{let x=atomicLoad(&a[2]);}', 'bounds')

    def test_source_closure_and_atomic_result_uniformity(self):
        self.compile('var<workgroup> a:atomic<u32>;fn hidden()->u32{return atomicLoad(&a);}'
                     '@fragment fn main(){let skipped=false&&(hidden()==0u);}', 'restricted to compute')
        self.compile(ATOMIC+'fn hidden()->u32{return atomicLoad(&a[0]);}'
                     '@vertex fn main()->@builtin(position) vec4f{return vec4f(1.0);let dead=hidden();}',
                     'writable storage')
        self.compile('var<workgroup> a:atomic<u32>;'+ENTRY+
                     '{if(atomicLoad(&a)==0u){workgroupBarrier();}}', 'uniform')
        self.compile('''var<workgroup> a:array<atomic<u32>,2>;
            fn index()->u32{workgroupBarrier();return 0u;}
            @compute @workgroup_size(2) fn main(@builtin(local_invocation_index) lane:u32){
                if(lane==0u){atomicStore(&a[index()],1u);}}''', 'uniform')
        self.compile(ATOMIC+'@fragment fn main(){atomicStore(&a[0],1u);}')

    def test_result_values_and_nested_argument_evaluation(self):
        code = self.compile(ATOMIC + '''
            @group(0) @binding(1) var<storage,read_write> cursor:atomic<u32>;
            @compute @workgroup_size(1) fn main(){
                let x=atomicAdd(&a[atomicAdd(&cursor,1u)],atomicAdd(&cursor,2u));
                let result=atomicCompareExchangeWeak(&a[0],x,5u);
                var values=array(result,result);let i=1i;
                values[i]=result;let old=values[i].old_value;
                let yes=values[i].exchanged;atomicStore(&a[0],old+u32(yes));
            }''')
        self.assertEqual(sum(op == 234 for op, _ in code), 3)
        self.assertEqual(sum(op == 230 for op, _ in code), 1)

    def test_retained_workgroup_atomic_sources(self):
        for name in ['workgroup_atomic.wgsl', 'workgroup_atomic_1024x100.wgsl']:
            code = self.compile((test_stages.ROOT/'bench/kernels'/name).read_text())
            self.assertTrue(any(op == 227 for op, _ in code))
            self.assertTrue(any(op == 228 for op, _ in code))
            self.assertGreaterEqual(sum(op == 224 for op, _ in code), 3)


if __name__ == '__main__':
    unittest.main()
