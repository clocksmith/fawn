#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Function-address pointer ownership, alias/lifetime and target controls."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_cli import decode
from function_machine import execute_functions

ROOT = Path(__file__).resolve().parents[4]
CLI = os.environ['DOE_COMPILER_CLI']
VALIDATOR = os.environ['SPIRV_VAL']
RESOURCE = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
ENTRY = '@compute @workgroup_size(1) fn main()'


class Pointers(unittest.TestCase):
    def compile(self, source, error=None):
        with tempfile.TemporaryDirectory(prefix='doe-pointer-') as directory:
            source_path = Path(directory) / 'case.wgsl'
            target = Path(directory) / 'case.spv'
            source_path.write_text(source)
            result = subprocess.run([CLI, '--shader-path', str(source_path), '--out', str(target)],
                                    capture_output=True, text=True)
            if error is not None:
                self.assertNotEqual(result.returncode, 0, source)
                self.assertIn(error, result.stderr)
                self.assertFalse(target.exists())
                return None
            self.assertEqual(result.returncode, 0, result.stderr)
            validated = subprocess.run([VALIDATOR, '--target-env', 'vulkan1.1', str(target)],
                                       capture_output=True, text=True)
            self.assertEqual(validated.returncode, 0, validated.stderr)
            instructions = decode(target.read_bytes())
            capabilities = {args[0] for opcode, args in instructions if opcode == 17}
            self.assertNotIn(4441, capabilities)  # VariablePointersStorageBuffer
            self.assertNotIn(4442, capabilities)  # VariablePointers
            return instructions

    def execute(self, declarations, body, expected):
        instructions = self.compile(RESOURCE + declarations + ENTRY + '{' + body + '}')
        memory = {(0, 0): [0xdeadbeef] * len(expected)}
        execute_functions(instructions, memory, 0)
        self.assertEqual(memory[(0, 0)], expected)

    def test_caller_identity_forwarding_and_snapshots(self):
        self.execute('''
            fn next(p:ptr<function,u32>)->u32{let old=*p;*p+=1u;return old;}
            fn forward(q:ptr<function,u32>)->u32{return next(q);}
            fn combine(a:u32,b:u32)->u32{return a*10u+b;}
        ''', '''
            var state=3u;
            let p=&state;let q=p;
            out[0]=combine(forward(q),next(&state));
            out[1]=state+next(p)+state;
            out[2]=state;
            *(&state)=8u;
            out[3]=*q;
        ''', [34, 16, 6, 8])

    def test_fresh_callee_locals_and_readonly_aliases(self):
        self.execute('''
            fn write(p:ptr<function,u32>){*p=12u;}
            fn read(a:ptr<function,u32>,b:ptr<function,u32>)->u32{return *a+*b;}
            fn value()->u32{var a=1u;write(&a);return a;}
        ''', 'var a=7u;out[0]=read(&a,&a);out[1]=value();out[2]=value();out[3]=a;',
        [14, 12, 12, 7])

    def test_aggregate_paths_and_snapshotted_oob(self):
        instructions = self.compile(RESOURCE + '''
            struct Pair{a:array<u32,2>,b:u32}
            fn update(p:ptr<function,Pair>,i:i32){(*p).a[i]+=4u;(*p).b=9u;}
        ''' + '''
            @compute @workgroup_size(1) fn main(@builtin(global_invocation_id) gid:vec3u){
                var a=Pair(array(1u,2u),3u);var i=i32(gid.x)-1;
                let p=&a.a[i];i+=1;*p=7u;out[0]=*p;out[1]=a.a[0];
                update(&a,i);out[2]=a.b;
            }
        ''')
        self.assertTrue(any(op == 65 for op, _ in instructions))
        self.assertGreaterEqual(sum(op == 250 for op, _ in instructions), 3)
        self.assertTrue(any(op == 55 for op, _ in instructions))

    def test_source_static_alias_effects(self):
        declarations = '''
            fn leaf(a:ptr<function,u32>,b:ptr<function,u32>)->bool{*a=*b;return true;}
            fn forward(a:ptr<function,u32>,b:ptr<function,u32>)->bool{return leaf(a,b);}
        '''
        for call in ('forward(&x,&x);', 'let ignored=false&&forward(&x,&x);',
                     'return;forward(&x,&x);', 'let p=&x;let q=p;forward(p,q);'):
            self.compile(declarations + ENTRY + '{var x=0u;' + call + '}', 'alias')
        self.compile(declarations + ENTRY + '{var x=1u;var y=2u;forward(&x,&y);}')
        self.compile('fn f(a:ptr<function,u32>,b:ptr<function,u32>){*a=1u;}' +
                     ENTRY + '{var x=0u;f(&x,&x);}', 'alias')

    def test_long_forwarding_graph(self):
        declarations = ''.join(
            f'fn f{i}(a:ptr<function,u32>,b:ptr<function,u32>){{f{i+1}(a,b);}}'
            for i in range(127))
        declarations += 'fn f127(a:ptr<function,u32>,b:ptr<function,u32>){*a=*b;}'
        self.compile(declarations + ENTRY + '{var x=1u;f0(&x,&x);}', 'alias')
        self.compile(declarations + ENTRY + '{var x=1u;var y=2u;f0(&x,&y);}')

    def test_addressability_lifetime_and_type_rejections(self):
        cases = [
            ('var x=vec2u();let p=&x.x;', 'not addressable'),
            ('var x=vec2u();let i=0u;let p=&x[i];', 'not addressable'),
            ('let x=1u;let p=&x;', 'addressable'),
            ('let p=&1u;', 'addressable'),
            ('var x=1u;var p=&x;', 'constructible'),
            ('var x=1u;const p=&x;', 'const initializer'),
            ('var x=1u;let p=&x;let pp=&p;', 'addressable'),
            ('var x=1u;let p=&x;let a=array(p);', 'constructible'),
            ('var x=1u;let p=&x;let y=p+1u;', 'overload'),
            ('var x=1u;let y=*x;', 'pointer value'),
            ('var x=1u;{let p=&x;}let y=*p;', 'name does not resolve'),
        ]
        for body, error in cases:
            self.compile(ENTRY + '{' + body + '}', error)
        self.compile('fn f()->ptr<function,u32>{var x=0u;return &x;}' + ENTRY + '{}', 'result declaration')
        self.compile('struct S{p:ptr<function,u32>}' + ENTRY + '{}', 'struct member')
        self.compile('fn f(p:ptr<function,u32>){}' + ENTRY + '{var a=array(1u,2u);f(&a[0]);}', 'unrestricted')
        self.compile('fn f(p:ptr<function,u32>){}' + ENTRY + '{var a=1i;f(&a);}', 'implicit conversion')
        self.compile('fn f(p:ptr<function,u32,read_write>){}' + ENTRY + '{}', 'access mode')

    def test_collective_boundary_and_independent_entries(self):
        self.compile(ENTRY + '{var x=0u;let p=&x;workgroupBarrier();}', 'dependency analysis')
        self.compile('fn f(p:ptr<function,u32>){*p=1u;}' + ENTRY +
                     '{var x=0u;f(&x);workgroupBarrier();}', 'dependency analysis')
        self.compile('fn f(p:ptr<function,u32>){*p=1u;}' + ENTRY + '{var x=0u;f(&x);}' +
                     '@compute @workgroup_size(1) fn sibling(){workgroupBarrier();}')

    def test_software_fixture(self):
        self.compile((Path(__file__).parent / 'pointer_operations.wgsl').read_text())

    def test_unchanged_monte_carlo_source(self):
        source = ROOT / 'bench/kernels/monte_carlo_paths_fixed_samples.wgsl'
        instructions = self.compile(source.read_text())
        self.assertGreater(sum(op == 57 for op, _ in instructions), 10)
        self.assertTrue(any(op == 32 and args[1] == 7 for op, args in instructions))


if __name__ == '__main__':
    unittest.main()
