#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Developer all-entry target checks; not the independently sealed M13 oracle.

Every target is validated whole. The retained M12 instruction model accepts one
entry metadata record, so its dispatch view filters only other OpEntryPoint
records, retaining all function bodies/variables/operations from the same binary.
Workgroup cases below inspect target structure, not concurrent execution.
"""
from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

from numeric_machine import execute_numeric
from test_cli import decode

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])


def entry_name(arguments):
    data = struct.pack('<' + 'I' * (len(arguments) - 2), *arguments[2:])
    return data.split(b'\0', 1)[0].decode('ascii')


def entries(code):
    return {entry_name(arguments): arguments[1] for opcode, arguments in code if opcode == 15}


def dispatch_view(code, name):
    identities = entries(code)
    assert name in identities
    return [(opcode, arguments) for opcode, arguments in code
            if opcode != 15 or arguments[1] == identities[name]]


class ComputeModules(unittest.TestCase):
    def compile(self, source):
        with tempfile.TemporaryDirectory(prefix='doe-m13-module-') as directory:
            shader = Path(directory) / 'module.wgsl'
            target = Path(directory) / 'module.spv'
            shader.write_text(source)
            result = subprocess.run([str(CLI), '--shader-path', str(shader), '--out', str(target)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(target)],
                                   capture_output=True, text=True)
            self.assertEqual(valid.returncode, 0, valid.stderr)
            return decode(target.read_bytes())

    def test_each_entry_executes_its_body_in_one_module(self):
        declarations = '''
            @group(0) @binding(0) var<storage,read_write> first:array<u32,2>;
            @group(1) @binding(2) var<storage,read_write> second:array<u32,7>;
            fn value(lane:u32)->u32{return 100u+lane;}
        '''
        small = '@compute @workgroup_size(1) fn tiny(){first[0]=1u;}'
        wide = '''@compute @workgroup_size(7) fn wide(
            @builtin(local_invocation_index) lane:u32){second[lane]=value(lane);}'''
        for bodies in (small + wide, wide + small):
            code = self.compile(declarations + bodies)
            exported = entries(code)
            self.assertEqual(set(exported), {'tiny', 'wide'})
            modes = {args[0]: args[2:] for op, args in code if op == 16}
            self.assertEqual(modes[exported['tiny']], (1, 1, 1))
            self.assertEqual(modes[exported['wide']], (7, 1, 1))
            self.assertEqual(sum(op == 54 for op, _ in code), 3)
            for selected in ('tiny', 'wide'):
                memory = {(0, 0): [0xdeadbeef] * 2, (1, 2): [0xdeadbeef] * 7}
                view = dispatch_view(code, selected)
                for lane in range(1 if selected == 'tiny' else 7):
                    execute_numeric(view, memory, lane, inputs={29: lane})
                self.assertEqual(memory[(0, 0)], [1, 0xdeadbeef] if selected == 'tiny' else [0xdeadbeef] * 2)
                self.assertEqual(memory[(1, 2)], list(range(100, 107)) if selected == 'wide' else [0xdeadbeef] * 7)

    def test_source_static_closure_does_not_execute_skipped_calls(self):
        code = self.compile('''
            @group(0) @binding(0) var<storage,read_write> a:array<u32>;
            @group(0) @binding(1) var<storage,read_write> b:array<u32>;
            fn touch()->bool{b[0]=9u;return true;}
            @compute @workgroup_size(1) fn main(){a[0]=1u;let skipped=false&&touch();return;a[1]=b[1];}
            @compute @workgroup_size(1) fn other(){b[0]=3u;}
        ''')
        memory = {(0, 0): [99, 98], (0, 1): [97, 96]}
        execute_numeric(dispatch_view(code, 'main'), memory, 0)
        self.assertEqual(memory, {(0, 0): [1, 98], (0, 1): [97, 96]})

    def test_per_entry_emitted_interfaces_and_hidden_input(self):
        code = self.compile('''
            @group(0) @binding(0) var<storage,read_write> out:array<u32>;
            var<workgroup> a:array<u32,2>;var<workgroup> b:array<u32,7>;
            @compute @workgroup_size(2) fn first(@builtin(local_invocation_id) lane:vec3u,
                @builtin(global_invocation_id) unused:vec3u){out[lane.x]=a[lane.x];}
            @compute @workgroup_size(7) fn second(@builtin(local_invocation_index) lane:u32,
                @builtin(workgroup_id) unused:vec3u){out[lane]=b[lane];return;out[0]=unused.x;}
            @compute @workgroup_size(1) fn empty(@builtin(local_invocation_id) unused:vec3u){}
        ''')
        decorated = {args[0]: args[2] for op, args in code if op == 71 and args[1] == 11}
        interfaces = {}
        for op, args in code:
            if op != 15:
                continue
            name = entry_name(args)
            name_words = len(name.encode()) // 4 + 1
            interfaces[name] = [decorated[ident] for ident in args[2 + name_words:]]
        self.assertEqual(interfaces, {'first': [27, 29], 'second': [29], 'empty': []})
        self.assertEqual(sum(op == 224 for op, _ in code), 2)
        # Each entry prologue stores only its one typed root, not its sibling.
        roots = {args[1] for op, args in code if op == 59 and args[2] == 4}
        stores = [args[0] for op, args in code if op == 62 and args[0] in roots]
        self.assertEqual(len(stores), 2)
        self.assertEqual(set(stores), roots)

    def test_aggregate_inputs_are_not_a_module_wide_five_slot_array(self):
        parameters = '''@builtin(global_invocation_id) g:vec3u,@builtin(local_invocation_id) l:vec3u,
            @builtin(workgroup_id) w:vec3u,@builtin(num_workgroups) n:vec3u,
            @builtin(local_invocation_index) i:u32'''
        source = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
        source += '@compute @workgroup_size(1) fn a(' + parameters + '){out[0]=g.x+l.x+w.x+n.x+i;}'
        source += '@compute @workgroup_size(1) fn b(' + parameters + '){out[0]=g.y+l.y+w.y+n.y+i;}'
        code = self.compile(source)
        decorated = {args[0] for op, args in code if op == 71 and args[1] == 11}
        self.assertEqual(len(decorated), 10)
        self.assertEqual(len(entries(code)), 2)


if __name__ == '__main__':
    unittest.main()
