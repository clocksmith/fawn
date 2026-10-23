#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""M9 owner developer checks; historical tests and independent fixtures untouched."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_cli import ROOT, VALIDATOR, decode, float_bits
from function_machine import execute_functions

RESOURCE = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
ENTRY = '@compute @workgroup_size(1) fn main()'
CLI = Path(os.environ.get('DOE_COMPILER_CLI', ROOT/'build/doe-compiler-c'))

class FunctionTests(unittest.TestCase):
    def compile(self, source, error=None, fixed=False):
        with tempfile.TemporaryDirectory(dir=ROOT/'build', prefix='functions-') as directory:
            path, output = Path(directory)/'a.wgsl', Path(directory)/'a.spv'
            path.write_text(source)
            command = [str(CLI), '--shader-path', str(path), '--out', str(output)]
            if fixed:
                command += ['--reflection', str(Path(directory)/'a.json')]
            run = subprocess.run(command, capture_output=True)
            if error is not None:
                self.assertNotEqual(run.returncode, 0, source)
                self.assertIn(error.encode(), run.stderr, run.stderr.decode())
                self.assertFalse(output.exists())
                return
            self.assertEqual(run.returncode, 0, run.stderr.decode())
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(output)], capture_output=True)
            self.assertEqual(valid.returncode, 0, valid.stderr.decode())
            return decode(output.read_bytes())

    def check(self, declarations, body, expected, initial=None, fixed=False):
        code = self.compile(RESOURCE + declarations + ENTRY + '{' + body + '}', fixed=fixed)
        memory = {(0, 0): list(initial if initial is not None else [99]*len(expected))}
        execute_functions(code, memory, 0)
        self.assertEqual(memory[(0, 0)], expected)
        return code

    def test_signatures_forward_and_discard(self):
        declarations = '''fn a(x:u32)->u32{return b(x)+1u;}
            fn b(x:u32)->u32{return x*2u;}
            fn empty(){} fn truth(x:bool)->bool{return !x;}
            fn signed_value(x:i32)->i32{return x-1;} fn real(x:f32)->f32{return x*2;}'''
        for fixed in (False, True):
            code = self.check(declarations,
                'empty(); a(9);out[0]=a(3);out[1]=u32(truth(false));out[2]=bitcast<u32>(signed_value(-1));out[3]=bitcast<u32>(real(1.5));',
                [7, 1, 0xfffffffe, float_bits(3)], fixed=fixed)
            self.assertEqual(sum(op == 54 for op, _ in code), 7)
            self.assertTrue(any(op == 55 for op, _ in code))
            self.assertTrue(any(op == 57 for op, _ in code))
            self.assertTrue(any(op == 254 for op, _ in code))

    def test_argument_order_fresh_locals_and_resource_reloads(self):
        self.check('''fn next()->u32{let old=out[0];out[0]=old+1u;return old;}
            fn join(a:u32,b:u32)->u32{var x=a*10u; x+=b;return x;}
            fn work(x:u32)->u32{var local=x;local+=join(next(),next());return local;}''',
            'out[1]=work(100);out[2]=work(200);out[3]=out[0]+next()+out[0];',
            [5, 101, 223, 13], [0, 0, 0, 0])
        self.check('fn a()->u32{var x:u32;x+=1u;return x;}',
                   'var sum=0u;for(var i=0u;i<4u;i++){sum+=a();}out[0]=sum;', [4])

    def test_control_and_lexical_names(self):
        self.check('''fn min(x:u32,y:u32)->u32{return x+y;}
            fn f(x:u32)->u32{var sum=x;{let x=3u;sum+=x;}if(sum>5u){return min(sum,1u);}else{return sum;}}''',
            'out[0]=f(4u);out[1]=f(1u);', [8, 4])
        self.check('fn f()->u32{loop{return 7u;}}', 'out[0]=f();', [7])
        self.check('fn side()->bool{out[0]+=1u;return true;}',
                   'if(false && side()){}if(true || side()){}out[1]=u32(side());', [1,1], [0,0])

    def test_runtime_and_const_builtins(self):
        self.check('', 'let x=out[0];out[1]=min(x,2u);out[2]=max(x,2u);let low=10u;let high=2u;out[3]=clamp(x,low,high);',
                   [5,2,5,2], [5,0,0,0])
        self.check('', 'let x=-4i;out[0]=bitcast<u32>(min(x,-3i));out[1]=bitcast<u32>(max(x,-3i));',
                   [0xfffffffc,0xfffffffd])
        self.check('', 'const a=exp(0);const b=tanh(0.0);const c=sqrt(9.0);out[0]=u32(a);out[1]=u32(b);out[2]=u32(c);', [1,0,3])
        code=self.check('fn f(x:f32)->f32{return exp(x)+tanh(x)+sqrt(x);}',
                        'out[0]=bitcast<u32>(f(0));', [float_bits(1)])
        self.assertEqual({args[3] for op,args in code if op==12}, {21,27,31})
        self.assertFalse(any(op==12 and args[3] in (43,44,45) for op,args in code))

    def test_signed_zero_policy(self):
        self.check('', 'out[0]=bitcast<u32>(max(0.0f,-0.0f));out[1]=bitcast<u32>(max(-0.0f,0.0f));let p=0.0f;let n=-0.0f;out[2]=bitcast<u32>(max(p,n));out[3]=bitcast<u32>(min(n,p));',
                   [0x80000000,0,0x80000000,0])

    def test_rejections(self):
        modules = [
            ('fn f()->u32{}', ''), ('fn f(){return 1u;}', ''),
            ('fn f()->u32{return;}', ''), ('fn f(x:u32){x=2u;}', ''),
            ('fn f(x:u32){let x=2u;}', ''), ('fn f(x:u32,x:u32){}', ''),
            ('fn f()->u32{return 1i;}', ''), ('fn f(x:u32){}', 'f(1i);'),
            ('fn f(){}', 'let x=f();'), ('@must_use fn f()->u32{return 1u;}', 'f();'),
            ('@must_use fn f(){}', ''), ('fn f(){}', 'let f=1u;f();'),
            ('fn f(){}', 'main();'), ('fn f()->u32{return 1u;}', 'const x=f();'),
            ('fn f()->u32{return 1u;}const X=f();', ''),
            ('fn f()->u32{return 1u;}', 'f(1u);'),
            ('fn f()->bool{return false && f();}', ''),
            ('fn f(){return;g();}fn g(){f();}', ''),
            ('fn f(){g();}fn g(){f();}', ''), ('fn f(){f();}', ''),
            ('fn f()->u32{return hidden;}', 'let hidden=1u;'),
            ('', 'min(1u,2u);'), ('', 'let x=exp(1u);'), ('', 'let x=min(true,false);'),
            ('', 'let x=min(1u,2i);'), ('', 'let x=sqrt(-1.0);'),
            ('', 'let x=out[0];let y=clamp(x,3u,2u);'),
            ('', 'if(false && (clamp(out[0],3u,2u)==0u)){}'),
            ('', 'let x=exp(1000.0f);'), ('', 'let x=max(1u,2u,3u);'),
        ]
        for declarations, body in modules:
            with self.subTest(declarations=declarations, body=body):
                self.compile(RESOURCE+declarations+ENTRY+'{'+body+'}', 'category=4')
        self.compile(RESOURCE+'fn f()->u32{return id.x;}@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){}', 'category=4')

    def test_entry_alias_and_for_calls(self):
        source = RESOURCE + 'fn f()->u32{return 7u;}' + '@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3u){let lane=id.x;out[lane]=f();}'
        code = self.compile(source, fixed=True)
        memory = {(0, 0): [0]}
        execute_functions(code, memory, 0)
        self.assertEqual(memory[(0, 0)], [7])
        self.check('fn tick(){out[0]+=1u;}', 'for(tick();out[0]<4u;tick()){}', [4], [0])

    def test_more_than_sixteen_formals(self):
        formals=','.join(f'p{i}:u32' for i in range(40))
        arguments=','.join(str(i)+'u' for i in range(40))
        self.check(f'fn sum({formals})->u32{{return p0+p39;}}', f'out[0]=sum({arguments});', [39], fixed=True)

if __name__ == '__main__':
    unittest.main()
