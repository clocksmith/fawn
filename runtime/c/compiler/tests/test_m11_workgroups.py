#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""M11 author checks. Independent acceptance cases and historical tests are separate."""
from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])
ENTRY = '@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane:u32,@builtin(workgroup_id) group:vec3u)'
READ = '@group(0) @binding(0) var<storage,read> src:array<u32>;'
WRITE = '@group(0) @binding(1) var<storage,read_write> dst:array<u32>;'


def instructions(data: bytes) -> list[tuple[int, tuple[int, ...]]]:
    words = struct.unpack(f'<{len(data)//4}I', data)
    result = []
    index = 5
    while index < len(words):
        size = words[index] >> 16
        assert size and index + size <= len(words)
        result.append((words[index] & 65535, words[index+1:index+size]))
        index += size
    return result


class WorkgroupTests(unittest.TestCase):
    def compile(self, source: str, error: str | None = None) -> list[tuple[int, tuple[int, ...]]]:
        with tempfile.TemporaryDirectory(prefix='doe-m11-case-') as directory:
            root = Path(directory)
            shader, output = root/'case.wgsl', root/'case.spv'
            shader.write_text(source, encoding='utf-8')
            run = subprocess.run([str(CLI), '--shader-path', str(shader), '--out', str(output)], capture_output=True)
            if error:
                self.assertNotEqual(run.returncode, 0, source)
                self.assertIn(error.encode(), run.stderr, (source, run.stderr.decode()))
                self.assertFalse(output.exists())
                return []
            self.assertEqual(run.returncode, 0, (source, run.stderr.decode()))
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(output)], capture_output=True)
            self.assertEqual(valid.returncode, 0, valid.stderr.decode())
            return instructions(output.read_bytes())

    def test_uniformity_acceptance(self) -> None:
        bodies = {
            'plain': 'workgroupBarrier();',
            'group_condition': 'if(group.x>0u){workgroupBarrier();}',
            'if_reconvergence': 'if(lane>0u){}workgroupBarrier();',
            'uniform_early_return': 'if(group.x>0u){return;}workgroupBarrier();',
            'whole_assignment_kills': 'var x=lane;x=0u;if(x==0u){workgroupBarrier();}',
            'both_branch_kills': 'var x=lane;if(group.x>0u){x=1u;}else{x=2u;}if(x>0u){workgroupBarrier();}',
            'loop_reconvergence': 'for(var i=lane;i<8u;i++){var y=i;}workgroupBarrier();',
            'loop_break_reconvergence': 'loop{if(lane>0u){break;}break;}workgroupBarrier();',
            'uniform_continue': 'for(var i=0u;i<4u;i++){if(i==2u){continue;}workgroupBarrier();}',
            'uniform_backedge': 'var x=0u;loop{workgroupBarrier();x++;continuing{break if x==4u;}}',
            'continuing_body_local': 'loop{let x=group.x;workgroupBarrier();continuing{break if x>0u;}}',
            'unreachable_barrier': 'return;if(lane>0u){workgroupBarrier();}',
            'unreachable_after_both_returns': 'if(lane==0u){return;}else{return;}workgroupBarrier();',
            'scope_restore': 'let x=group.x;{let x=lane;}if(x>0u){workgroupBarrier();}',
            'immutable_snapshot': 'var x=0u;let y=x;x=lane;if(y==0u){workgroupBarrier();}',
            'uniform_readonly': 'if(src[group.x]>0u){workgroupBarrier();}',
            'uniform_short_circuit': 'if(true&&(group.x>0u)){workgroupBarrier();}',
            'for_shadow': 'let x=group.x;for(var x=lane;x<4u;x++){}if(x>0u){workgroupBarrier();}',
            'zero_local': 'var x:u32;if(x==0u){workgroupBarrier();}',
            'nested_break': 'loop{loop{if(lane>0u){break;}break;}workgroupBarrier();break;}',
        }
        for name, body in bodies.items():
            with self.subTest(name=name):
                self.compile(READ+ENTRY+'{'+body+'}')

    def test_uniformity_rejection(self) -> None:
        bodies = {
            'input': 'if(lane>0u){workgroupBarrier();}',
            'early_return': 'if(lane>0u){return;}workgroupBarrier();',
            'varying_store': 'var x=0u;if(lane>0u){x=1u;}if(x>0u){workgroupBarrier();}',
            'snapshot_not_replayed': 'var x=lane;let y=x;x=0u;if(y>0u){workgroupBarrier();}',
            'constant_skipped_value': 'if(false&&(lane>0u)){workgroupBarrier();}',
            'index_dep': 'if(src[lane]>0u){workgroupBarrier();}',
            'mutable_root': 'if(dst[0]>0u){workgroupBarrier();}',
            'local_const_control': 'if(lane>0u){const x=true;if(x){workgroupBarrier();}}',
            'varying_continue': 'for(var i=0u;i<4u;i++){if(i==lane){continue;}workgroupBarrier();}',
            'later_iteration_break': 'loop{workgroupBarrier();if(lane>0u){break;}}',
            'later_iteration_continue': 'loop{workgroupBarrier();if(lane>0u){continue;}break;}',
            'continuing_break_if': 'loop{workgroupBarrier();continuing{break if lane>0u;}}',
            'loop_return_then_barrier': 'loop{if(lane>0u){return;}break;}workgroupBarrier();',
            'loop_value_fixedpoint': 'var x=0u;loop{if(x>0u){workgroupBarrier();}x=lane;if(group.x>0u){break;}}',
            'partial_array': 'var x=array<u32,2>(lane,0u);x[0]=0u;if(x[0]==0u){workgroupBarrier();}',
            'partial_vector': 'var x=vec2u(lane,0u);x.x=0u;if(x.x==0u){workgroupBarrier();}',
            'partial_index': 'var x=array<u32,2>(0u,0u);x[lane]=0u;if(x[0]==0u){workgroupBarrier();}',
        }
        for name, body in bodies.items():
            with self.subTest(name=name):
                self.compile(READ+WRITE+ENTRY+'{'+body+'}', 'uniform control cannot be established')

    def test_function_summaries(self) -> None:
        cases = [
            ('fn sync(){workgroupBarrier();}', 'sync();', True),
            ('fn sync(){workgroupBarrier();}', 'if(lane>0u){sync();}', False),
            ('fn sync(x:u32){if(x>0u){workgroupBarrier();}}', 'sync(group.x);', True),
            ('fn sync(x:u32){if(x>0u){workgroupBarrier();}}', 'sync(lane);', False),
            ('fn pick(a:u32,b:u32)->u32{return b;} fn sync(x:u32){if(x>0u){workgroupBarrier();}}', 'sync(pick(lane,group.x));', True),
            ('fn pick(a:u32,b:u32)->u32{return b;} fn sync(x:u32){if(x>0u){workgroupBarrier();}}', 'sync(pick(group.x,lane));', False),
            ('fn sync()->bool{workgroupBarrier();return true;}', 'if(lane>0u){let x=false&&sync();}', False),
            ('fn sync()->bool{workgroupBarrier();return true;}', 'let x=false&&sync();', True),
            ('fn f(x:u32)->u32{if(x>0u){return 0u;}return 1u;}', 'if(f(lane)==0u){workgroupBarrier();}', False),
            ('fn f(x:u32){if(x>0u){return;}}', 'f(lane);workgroupBarrier();', True),
            ('fn f()->u32{return dst[0];}', 'if(f()==0u){workgroupBarrier();}', False),
            ('fn f(x:u32)->u32{var y=x;y=0u;return y;}', 'if(f(lane)==0u){workgroupBarrier();}', True),
            ('fn f(x:u32){g(x);}fn g(x:u32){if(x>0u){workgroupBarrier();}}', 'f(group.x);', True),
            ('fn f(x:u32){g(x);}fn g(x:u32){if(x>0u){workgroupBarrier();}}', 'f(lane);', False),
        ]
        for declarations, body, accepted in cases:
            with self.subTest(body=body, declarations=declarations):
                self.compile(WRITE+declarations+ENTRY+'{'+body+'}', None if accepted else 'uniform control cannot be established')

    def test_roots_prologue_and_shadowing(self) -> None:
        declarations = 'struct S{b:bool,v:vec3u,a:array<f32,2>,};var<workgroup> a:S;var<workgroup> b:u32;'
        for entry in (ENTRY, '@compute @workgroup_size(1) fn main()'):
            code = self.compile(declarations+entry+'{return;}')
            roots = {operands[1] for op, operands in code if op==59 and operands[2]==4}
            self.assertEqual(len(roots), 2)
            nulls = {operands[1] for op, operands in code if op==46}
            stores = [operands for op, operands in code if op==62 and operands[0] in roots]
            self.assertEqual(len(stores), 2)
            self.assertTrue(all(operands[1] in nulls for operands in stores))
            constants = {operands[1]: operands[2] for op, operands in code if op==43}
            barriers = [operands for op, operands in code if op==224]
            self.assertEqual(len(barriers), 1)
            self.assertEqual(tuple(constants[x] for x in barriers[0]), (2, 2, 0x108))
            self.assertFalse(any(op==71 and operands[0] in roots for op, operands in code))
            self.assertEqual(sum(op==71 and operands[1:]==(11,29) for op, operands in code), 1)
        self.compile('var<workgroup> x:u32;fn f(){x=2u;}'+ENTRY+'{let x=lane;f();workgroupBarrier();}')
        self.compile('fn workgroupBarrier(){}'+ENTRY+'{if(lane>0u){workgroupBarrier();}}')
        self.compile('struct workgroupBarrier{x:u32,}'+ENTRY+'{let x=workgroupBarrier(lane);}')
        self.compile('var<workgroup> x:u32;'+ENTRY+'{workgroupBarrier();if(x>0u){workgroupBarrier();}}', 'uniform control cannot be established')
        self.compile('var<workgroup> x:u32;'+ENTRY+'{let x=lane;if(x>0u){workgroupBarrier();}}', 'uniform control cannot be established')
        code = self.compile(ENTRY+'{workgroupBarrier();}')
        self.assertFalse(any(op==59 and operands[2]==4 for op, operands in code))
        self.assertEqual(sum(op==224 for op, operands in code), 1)

    def test_invalid_workgroup_forms(self) -> None:
        cases = [
            ('var<workgroup> x:u32=0u;', 'cannot have an initializer'),
            ('var<workgroup,read_write> x:u32;', 'cannot specify an access mode'),
            ('var<workgroup> x:array<u32>;', 'fixed-footprint'),
            ('var<workgroup> x:atomic<u32>;', 'not supported'),
            ('var<workgroup> x:u32;const x=1u;', 'duplicate module declaration'),
            ('var<workgroup> u32:u32;', 'type name resolves to a module value'),
        ]
        for declarations, error in cases:
            with self.subTest(declarations=declarations):
                self.compile(declarations+ENTRY+'{}', error)
        for body in ('workgroupBarrier(1u);', 'let x=workgroupBarrier();'):
            self.compile(ENTRY+'{'+body+'}', 'requires no arguments and no result')
        self.compile(ENTRY+'{let workgroupBarrier=1u;workgroupBarrier();}', 'lexical value')
        self.compile(ENTRY+'{return;let x=missing;}', 'name does not resolve')
        self.compile(ENTRY+'{storageBarrier();}', 'outside the workgroup barrier owner')


    def test_uniformity_work_limit(self) -> None:
        # All formals are analyzed, even unused ones; no call-site specialization.
        def shader(count: int) -> str:
            formals = ','.join(f'p{i}:u32' for i in range(count))
            return 'fn many('+formals+'){'+('loop{break;}'*20)+'}'+ENTRY+'{workgroupBarrier();}'
        self.compile(shader(64))
        self.compile(shader(750), 'category=3')
        self.compile(shader(750), 'uniformity analysis work limit exceeded')


if __name__ == '__main__':
    unittest.main()
