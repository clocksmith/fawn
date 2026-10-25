#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Workgroup target identity and aggregate transfer; no physical GPU claim."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import test_stages

# Read-only reuse of the retained independent word executor. Neither its source,
# frozen inputs nor acceptance receipts are changed by these developer cases.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'independent_tests_m11/oracle'))
from target_m11 import Module
from workgroup_vm import dispatch


def words(code):
    result = [0x07230203, 0x00010300, 0, 0x3fffff, 0]
    for op, operands in code:
        result.extend([((len(operands) + 1) << 16) | op, *operands])
    return result


class WorkgroupTypes(unittest.TestCase):
    compile = test_stages.Stages.compile

    def test_barrier_memory_classes_and_uniformity(self):
        code = self.compile('@compute @workgroup_size(4) fn main(){'
                            'workgroupBarrier();storageBarrier();textureBarrier();}')
        constants = {a[1]: a[2] for op, a in code if op == 43}
        barriers = [[constants[v] for v in a] for op, a in code if op == 224]
        self.assertEqual(barriers, [[2, 2, 0x108], [2, 2, 0x48], [2, 2, 0x808]])
        for builtin in ['storageBarrier', 'textureBarrier']:
            self.compile('fn helper(){' + builtin + '();}'
                '@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) i:u32){'
                'if(i>0u){helper();}}', 'uniform')
            self.compile('fn helper(){' + builtin + '();}'
                '@fragment fn main()->@location(0) vec4f{helper();return vec4f(0);}', 'compute entry')
            self.compile('@compute @workgroup_size(1) fn main(){' + builtin + '(1u);}', 'no arguments')
            self.compile('@compute @workgroup_size(1) fn main(){let x=' + builtin + '();}', 'no result')

    def test_workgroup_uniform_load_address_and_result(self):
        code = self.compile('var<workgroup> values:array<vec2u,2>;'
            '@compute @workgroup_size(4) fn main(){let value=workgroupUniformLoad(&values[1]);'
            'if(value.x>0u){workgroupBarrier();}values[1]=vec2u(1);}')
        self.assertEqual(sum(op == 224 for op, _ in code), 4)  # initialization, before/after load, conditional
        self.compile('var<workgroup> value:atomic<u32>;'
            '@compute @workgroup_size(4) fn main(){let v=workgroupUniformLoad(&value);'
            'if(v>0u){workgroupBarrier();}}')
        for declaration, address, error in [
            ('var<workgroup> values:array<u32,4>;', '&values[i]', 'uniform'),
            ('var<private> value:u32;', '&value', 'workgroup address'),
            ('var<workgroup> values:array<atomic<u32>,2>;', '&values', 'constructible'),
        ]:
            self.compile(declaration + '@compute @workgroup_size(4) fn main('
                '@builtin(local_invocation_index) i:u32){let x=workgroupUniformLoad(' + address + ');}', error)

    def test_device_independent_workgroup_dimensions(self):
        for dimensions in [(512, 1, 1), (16, 32, 2), (4294967295, 1, 1)]:
            code = self.compile('@compute @workgroup_size(' + ','.join(str(v) + 'u' for v in dimensions) +
                                ') fn main(){}')
            self.assertEqual([a[2:] for op, a in code if op == 16 and a[1] == 17], [dimensions])
        for dimensions in ['0,1,1', '65536u,65536u,1u', '4294967295u,2u,1u']:
            self.compile('@compute @workgroup_size(' + dimensions + ') fn main(){}', 'product must fit u32')

    def test_private_storage_in_graphics_has_no_shared_barrier_or_hidden_lane(self):
        code = self.compile('''struct State { values:array<vec4f,2> }
            var<private> state=State(array<vec4f,2>(vec4f(0.0),vec4f(1.0)));
            fn value()->vec4f{state.values[1].x+=1.0;return state.values[1];}
            @vertex fn vertex()->@builtin(position) vec4f{return value();}
            @fragment fn fragment()->@location(0) vec4f{return value();}
        ''')
        types = {a[0]: (op, a[1:]) for op, a in code if 19 <= op <= 33}
        roots = [types[a[0]][1][1] for op, a in code if op == 59 and a[2] == 6]
        self.assertEqual(len(roots), 1)
        self.assertFalse(any(op == 224 for op, _ in code))
        self.assertFalse(any(op == 71 and a[1:] == (11, 29) for op, a in code))
        decorated = {a[0] for op, a in code if op in (71, 72)}
        pending, seen = roots[:], set()
        while pending:
            typ = pending.pop()
            if typ in seen:
                continue
            seen.add(typ)
            op, operands = types[typ]
            self.assertNotIn(typ, decorated)
            pending.extend(operands if op == 30 else [operands[0]] if op in (23, 24, 28) else [])

    def test_shared_source_type_has_separate_undecorated_workgroup_graph(self):
        code = self.compile('''
            struct Record { tag:u32, values:array<vec2u,2> }
            alias Rows=array<Record,2>;
            @group(0) @binding(0) var<storage,read> src:Rows;
            @group(0) @binding(1) var<storage,read_write> dst:array<Record>;
            var<workgroup> cache:Rows;
            @compute @workgroup_size(2) fn main(@builtin(local_invocation_index) lane:u32) {
                if(lane==0u){cache=src;}
                workgroupBarrier();
                let snapshot=cache;
                let out_of_range=lane+4u;
                let outside=cache[out_of_range];
                cache[out_of_range]=src[0];
                dst[lane]=snapshot[1u-lane];
                if(lane==0u){dst[2]=outside;}
            }
        ''')
        types = {a[0]: (op, a[1:]) for op, a in code if 19 <= op <= 33}
        workgroup = [types[a[0]][1][1] for op, a in code if op == 59 and a[2] == 4]
        decorated = {a[0] for op, a in code if op in (71, 72)}
        pending, seen = workgroup[:], set()
        while pending:
            typ = pending.pop()
            if typ in seen:
                continue
            seen.add(typ)
            op, a = types[typ]
            self.assertNotIn(typ, decorated)
            pending.extend(a if op == 30 else [a[0]] if op in (23, 24, 28) else [])
        self.assertTrue(any(op == 71 and a[1] == 6 for op, a in code))
        rows = [11, 0, 1, 2, 3, 4, 22, 0, 5, 6, 7, 8]
        expected = rows[6:] + rows[:6] + [0] * 6
        for index in [1, 7, 13]:
            expected[index] = 0xdeadbeef

        def run(instructions):
            buffers = {(0, 0): bytearray(struct.pack('<12I', *rows)),
                       (0, 1): bytearray(struct.pack('<18I', *([0xdeadbeef] * 18)))}
            dispatch(Module(words(instructions)), buffers, schedule='reverse', pure_quantum=1)
            return list(struct.unpack('<18I', buffers[(0, 1)]))

        self.assertEqual(run(code), expected)
        mutated = code[:]
        for i, (op, a) in enumerate(mutated):
            if op == 81 and len(a) == 5 and a[3:] == (0, 0):
                mutated[i] = op, (*a[:3], 1, 0)
                break
        else:
            self.fail('no source aggregate tag extraction to mutate')
        with tempfile.TemporaryDirectory(prefix='doe-workgroup-mutation-') as directory:
            target = Path(directory) / 'wrong-tag.spv'
            target.write_bytes(struct.pack('<' + 'I' * len(words(mutated)), *words(mutated)))
            valid = subprocess.run([str(test_stages.VALIDATOR), '--target-env', 'vulkan1.1', str(target)],
                                   capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertNotEqual(run(mutated), expected)

    def test_matrix_and_nested_struct_transfer(self):
        self.compile('''
            struct Tail { value:u32 }
            struct Data { matrix:array<mat2x2f,2>, tail:Tail }
            @group(0) @binding(0) var<storage,read_write> data:Data;
            var<workgroup> cache:Data;
            @compute @workgroup_size(1) fn main(){cache=data;data=cache;}
        ''')

    def test_unchanged_tiled_matmul_consumers(self):
        for path in ['bench/kernels/compilation-corpus/complex_matmul_tiled.wgsl',
                     'bench/kernels/shader_robustness_matmul_512_f32.wgsl']:
            self.compile((test_stages.ROOT / path).read_text())


if __name__ == '__main__':
    unittest.main()
