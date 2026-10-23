#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Developer subgroup type/word/uniformity checks, not collective execution.

No sequential numeric oracle substitutes for actual subgroup execution.
SPIR-V validation establishes format, not physical device feature support.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_cli import decode

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])
ENABLE = 'enable subgroups; requires subgroup_uniformity;'
ENTRY = '@compute @workgroup_size(8) fn main(@builtin(local_invocation_index) lane:u32)'
RESOURCE = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
OPERATIONS = {
    'subgroupAdd': ((349, 349, 350), 0),
    'subgroupMin': ((353, 354, 355), 0),
    'subgroupMax': ((356, 357, 358), 0),
    'subgroupMul': ((351, 351, 352), 0),
    'subgroupAnd': ((359, 359, None), 0),
    'subgroupOr': ((360, 360, None), 0),
    'subgroupXor': ((361, 361, None), 0),
    'subgroupExclusiveAdd': ((349, 349, 350), 2),
    'subgroupInclusiveAdd': ((349, 349, 350), 1),
    'subgroupExclusiveMul': ((351, 351, 352), 2),
    'subgroupInclusiveMul': ((351, 351, 352), 1),
}


class Subgroups(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-subgroups-') as directory:
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
            self.assertEqual(valid.returncode, 0, valid.stderr)
            return decode(target.read_bytes())

    def test_arithmetic_signature_and_target_matrix(self):
        accepted = rejected = 0
        for name, (opcodes, operation) in OPERATIONS.items():
            for leaf_index, leaf in enumerate(('i32', 'u32', 'f32')):
                for width in (1, 2, 3, 4):
                    constructor = leaf if width == 1 else f'vec{width}<{leaf}>'
                    source = ENABLE + ENTRY + '{let value=' + name + '(' + constructor + '(2));}'
                    with self.subTest(name=name, leaf=leaf, width=width):
                        if opcodes[leaf_index] is None:
                            self.compile(source, 'no matching numeric builtin overload')
                            rejected += 1
                            continue
                        code = self.compile(source)
                        operations = [(op, args) for op, args in code if 349 <= op <= 361]
                        self.assertEqual(len(operations), 1)
                        opcode, args = operations[0]
                        self.assertEqual((opcode, args[3]), (opcodes[leaf_index], operation))
                        constants = {args[1]: args[2] for op, args in code if op == 43}
                        self.assertEqual(constants[args[2]], 3, 'execution scope must be Subgroup')
                        self.assertEqual({args[0] for op, args in code if op == 17}, {1, 61, 63})
                        self.assertFalse(any(op == 224 for op, _ in code), 'no synthesized workgroup barrier')
                        accepted += 1
        self.assertEqual((accepted, rejected), (120, 12))

    def test_runtime_stage_and_source_validation(self):
        self.compile(ENTRY + '{let x=subgroupAdd(1u);}', 'require enable subgroups')
        self.compile(ENABLE + 'const x=subgroupAdd(1u);' + ENTRY + '{}', 'runtime function')
        self.compile(ENABLE + ENTRY + '{const x=subgroupAdd(1u);}', 'const')
        self.compile(ENABLE + ENTRY + '{let x=subgroupAdd(true);}', 'numeric builtin arguments')
        self.compile(ENABLE + ENTRY + '{subgroupAdd(1u);}', 'used result')
        self.compile(ENABLE + ENTRY + '{let x=subgroupAdd(1u,2u);}', 'exact arguments')
        self.compile(ENABLE + ENTRY + '{return;let x=subgroupAnd(1.0);}', 'no matching numeric builtin overload')
        self.compile(ENTRY + '{return;let x=subgroupAdd(1u);}', 'require enable subgroups')
        code = self.compile(ENABLE + ENTRY + '{let x=subgroupAdd(1);}')
        self.assertTrue(any(op == 349 for op, _ in code), 'abstract input materializes to runtime i32')

    def test_scopes_are_distinct_and_transitive(self):
        helper = 'fn reduced()->u32{return subgroupAdd(1u);}'
        self.compile(ENABLE + helper + ENTRY + '{if reduced()>0u {let x=subgroupMul(2u);}}')
        self.compile(ENABLE + helper + ENTRY + '{if reduced()>0u {workgroupBarrier();}}',
                     'uniform control cannot be established')
        self.compile(ENABLE + ENTRY + '{if subgroupInclusiveAdd(1u)>0u {let x=subgroupAdd(2u);}}',
                     'subgroup uniform control cannot be established')
        self.compile(ENABLE + ENTRY + '{if lane>0u {let x=subgroupAdd(2u);}}',
                     'subgroup uniform control cannot be established')
        # The dated parameter-return dependency remains, even for reductions.
        self.compile(ENABLE + ENTRY + '{if subgroupAdd(lane)>0u {let x=subgroupAdd(2u);}}',
                     'subgroup uniform control cannot be established')
        effect = 'fn effect()->bool{let x=subgroupAdd(1u);return true;}'
        self.compile(ENABLE + effect + ENTRY + '{if lane>0u {let skipped=false&&effect();}}',
                     'subgroup uniform control cannot be established')

    def test_directives_namespace_and_no_unused_capabilities(self):
        self.compile('enable subgroups, subgroups,;' + ENTRY + '{}')
        self.compile('requires subgroup_uniformity,;' + ENTRY + '{}')
        self.compile('enable unsupported;' + ENTRY + '{}', 'unsupported enable or language extension')
        self.compile('const x=1;enable subgroups;' + ENTRY + '{}', 'directives must precede declarations')
        self.compile('enable;' + ENTRY + '{}', 'extension name')
        self.compile('enable subgroups subgroups;' + ENTRY + '{}', 'expected ;')
        code = self.compile(ENABLE + ENTRY + '{}')
        self.assertEqual({args[0] for op, args in code if op == 17}, {1})
        code = self.compile('fn subgroupAdd(x:u32)->u32{return x+1u;}' + ENTRY + '{let x=subgroupAdd(2u);}')
        self.assertFalse(any(349 <= op <= 361 for op, _ in code))
        self.compile(ENABLE + ENTRY + '{let subgroupAdd=2u;let x=subgroupAdd(1u);}', 'lexical value')

    def test_calls_and_all_entries_preserve_collectives(self):
        helper = 'fn partial(x:u32)->u32{out[x]=7u;return x+1u;}'
        source = ENABLE + RESOURCE + helper + ENTRY + '{out[lane]=subgroupAdd(partial(lane));}'
        source += '@compute @workgroup_size(4) fn second(){out[0]=subgroupExclusiveMul(2u);}'
        code = self.compile(source)
        self.assertEqual(sum(op == 57 for op, _ in code), 1, 'one argument evaluation')
        self.assertEqual(sum(op == 15 for op, _ in code), 2)
        self.assertEqual([(op, args[3]) for op, args in code if 349 <= op <= 361], [(349, 0), (351, 2)])

    def test_subgroup_inputs_have_scalar_types_and_exact_interfaces(self):
        from test_m13_modules import entry_name
        names = ('subgroup_size', 'subgroup_invocation_id', 'num_subgroups', 'subgroup_id')
        for name, decoration in zip(names, (36, 41, 38, 40)):
            with self.subTest(name=name):
                source = ENABLE + RESOURCE + '@compute @workgroup_size(8) fn main('
                source += '@builtin(' + name + ') value:u32){out[0]=value;}'
                code = self.compile(source)
                decorated = {args[0]: args[2] for op, args in code if op == 71 and args[1] == 11}
                interfaces = next(args for op, args in code if op == 15)
                name_words = len(entry_name(interfaces)) // 4 + 1
                interface = interfaces[2 + name_words:]
                self.assertEqual([decorated[item] for item in interface], [decoration])
                pointers = {args[0]: args[2] for op, args in code if op == 32}
                scalar_types = {args[0] for op, args in code if op == 21 and args[1:] == (32, 0)}
                inputs = {args[1]: args[0] for op, args in code if op == 59 and args[2] == 1}
                self.assertIn(pointers[inputs[interface[0]]], scalar_types)
                self.assertEqual({args[0] for op, args in code if op == 17}, {1, 61})
                self.compile(source.removeprefix(ENABLE), 'subgroup inputs require enable')
        entry = '@compute @workgroup_size(8) fn main(@builtin(subgroup_id) group:u32)'
        self.compile(ENABLE + entry + '{if group==0u {let x=subgroupAdd(1u);}}')
        self.compile(ENABLE + entry + '{if group==0u {workgroupBarrier();}}',
                     'uniform control cannot be established')
        for name in ('subgroup_size', 'num_subgroups'):
            entry = '@compute @workgroup_size(8) fn main(@builtin(' + name + ') size:u32)'
            self.compile(ENABLE + entry + '{if size>0u {workgroupBarrier();}}')


if __name__ == '__main__':
    unittest.main()
