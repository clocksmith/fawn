#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""Bounded M12 author checks, separate from sealed independent acceptance."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from numeric_machine import execute_numeric
from test_cli import decode, f32, float_bits

CLI = Path(os.environ['DOE_COMPILER_CLI'])
VALIDATOR = Path(os.environ['SPIRV_VAL'])
RESOURCE = '@group(0) @binding(0) var<storage,read_write> out:array<u32>;'
ENTRY = '@compute @workgroup_size(1) fn main()'


def vector(leaf: str, width: int, values: list[int]) -> str:
    suffix = {'ai': '', 'af': '.0', 'i32': 'i', 'u32': 'u', 'f32': '.0f'}[leaf]
    lanes = [str(value) + suffix for value in values[:width]]
    return lanes[0] if width == 1 else f'vec{width}(' + ','.join(lanes) + ')'


class NumericBuiltins(unittest.TestCase):
    def compile(self, source: str, error: str | None = None):
        with tempfile.TemporaryDirectory(prefix='doe-m12-case-') as directory:
            shader, output = Path(directory)/'a.wgsl', Path(directory)/'a.spv'
            shader.write_text(source)
            run = subprocess.run([str(CLI), '--shader-path', str(shader), '--out', str(output)], capture_output=True)
            if error is not None:
                self.assertNotEqual(run.returncode, 0, source)
                self.assertIn(error.encode(), run.stderr, (source, run.stderr.decode()))
                self.assertFalse(output.exists())
                return None
            self.assertEqual(run.returncode, 0, (source, run.stderr.decode()))
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(output)], capture_output=True)
            self.assertEqual(valid.returncode, 0, valid.stderr.decode())
            return decode(output.read_bytes())

    def execute(self, body: str, expected: list[int], declarations: str = '', initial=None):
        code = self.compile(RESOURCE + declarations + ENTRY + '{' + body + '}')
        memory = {(0, 0): list(initial if initial is not None else [99] * len(expected))}
        execute_numeric(code, memory, 0)
        self.assertEqual(memory[(0, 0)], expected)
        return code

    def test_first_decisive_reversed_clamp_and_modular_dot(self):
        declarations = '''fn arg(stamp:u32)->vec4u {
            let index=out[0];out[index+8u]=stamp;out[0]=index+1u;
            return vec4u(stamp,4294967295u,stamp+2u,65536u);
        }'''
        code = self.execute('''let r=clamp(arg(5u),arg(10u),arg(2u));
            out[1]=r.x;out[2]=r.y;out[3]=r.z;out[4]=r.w;
            let a=vec4u(65536u,4294967295u,2147483648u,3u);
            let b=vec4u(65536u,4294967295u,2u,7u);out[5]=dot(a,b);''',
            [3,2,0xffffffff,4,65536,22,99,99,5,10,2,99], declarations,
            [0]+[99]*11)
        self.assertEqual(sum(op == 57 for op, _ in code), 3)
        self.assertFalse(any(op == 11 for op, _ in code))
        self.assertFalse(any(op == 12 and args[3] in (43,44,45) for op,args in code))

    def test_argument_snapshot_and_swizzle(self):
        declarations = '''fn change()->vec2u {out[0]=100u;out[1]=200u;return vec2u(2u,3u);}'''
        self.execute('out[2]=dot(vec2u(out[0],out[1]).yx,change());',
                     [100,200,31], declarations, [5,8,99])
        self.execute('const a=dot(vec2(4294967296,4294967296),vec2(4294967296,-4294967296));out[0]=u32(a);', [0])

    def test_full_signature_matrix(self):
        families = [
            ('min',2,'all','all'), ('max',2,'all','all'), ('clamp',3,'all','all'),
            ('exp',1,'float','all'), ('tanh',1,'float','all'), ('sqrt',1,'float','all'),
            ('dot',2,'all','vector'), ('length',1,'float','all'), ('distance',2,'float','all'),
            ('normalize',1,'float','vector'), ('cross',2,'float','three')]
        counts = [0,0]
        for name, arity, leaves, shapes in families:
            for leaf in (('ai','af','i32','u32','f32') if leaves == 'all' else ('af','f32')):
                for width in ((1,2,3,4) if shapes == 'all' else (2,3,4) if shapes == 'vector' else (3,)):
                    arguments = [vector(leaf,width,[2,3,4,5]), vector(leaf,width,[1,2,3,4]), vector(leaf,width,[6,7,8,9])][:arity]
                    if name == 'clamp':
                        arguments = [arguments[0], arguments[1], arguments[2]]
                    result_width = 1 if name in ('dot','length','distance') else width
                    stores = ''.join(f'out[{i}]=bitcast<u32>(f32(r' + (f'.{"xyzw"[i]}' if result_width > 1 else '') + '));' for i in range(result_width))
                    with self.subTest(name=name, leaf=leaf, width=width, stage='const'):
                        code = self.compile(RESOURCE + f'const r={name}(' + ','.join(arguments) + ');' + ENTRY + '{' + stores + '}')
                        execute_numeric(code, {(0,0): [0]*result_width}, 0)
                        self.assertFalse(any(op == 11 for op,_ in code))
                        counts[0] += 1
                    if leaf in ('i32','u32','f32'):
                        declarations = ''.join(f'let a{i}={argument};' for i,argument in enumerate(arguments))
                        body = declarations + f'let r={name}(' + ','.join(f'a{i}' for i in range(arity)) + ');' + stores
                        with self.subTest(name=name, leaf=leaf, width=width, stage='runtime'):
                            code = self.compile(RESOURCE + ENTRY + '{' + body + '}')
                            execute_numeric(code, {(0,0): [0]*result_width}, 0)
                            counts[1] += 1
        self.assertEqual(counts, [123,69])

    def test_exact_integer_interpretation(self):
        expressions = [
            ('dot(vec3(9223372036854775807,1,-1),vec3(1,1,1))==9223372036854775807', 1),
            ('dot(vec2(-9223372036854775807-1,0),vec2(1,1))==(-9223372036854775807-1)', 1),
            ('dot(vec4(-9223372036854775807-1,-9223372036854775807-1,9223372036854775807,9223372036854775807),vec4(9223372036854775807,-9223372036854775807,9223372036854775807,-9223372036854775807))==0', 1),
            ('dot(vec2i(65536,65536),vec2i(65536,-65536))==0i', 1),
            ('dot(vec2u(4294967295u,4294967295u),vec2u(4294967295u,4294967295u))==2u', 1)]
        for expression, expected in expressions:
            self.execute(f'const r={expression};out[0]=u32(r);',[expected])
        failures = [
            'dot(vec2(9223372036854775807,1),vec2(1,1))',
            'dot(vec2(-9223372036854775807-1,-1),vec2(1,1))',
            'dot(vec4(-9223372036854775807-1),vec4(-9223372036854775807-1))']
        for expression in failures:
            self.compile('const r='+expression+';'+ENTRY+'{}','outside signed64 range')
        self.compile('const r=dot(vec2(4294967296*4294967296,0),vec2(1,1));'+ENTRY+'{}','overflow')
        self.compile(ENTRY+'{let r=dot(vec2(2147483647,1),vec2(1,1));}','out of range')

    def test_geometry(self):
        for stage in ('const','let'):
            self.execute(f'{stage} r=cross(vec3f(1,2,3),vec3f(4,5,6));out[0]=bitcast<u32>(r.x);out[1]=bitcast<u32>(r.y);out[2]=bitcast<u32>(r.z);',
                         [float_bits(-3),float_bits(6),float_bits(-3)])
            body = f'{stage} a=vec2f(3,4);let r=length(a);out[0]=bitcast<u32>(r);out[1]=bitcast<u32>(distance(-2.0f,3.0f));out[2]=bitcast<u32>(length(-7.0f));'
            self.execute(body,[float_bits(5),float_bits(5),float_bits(7)])
        code = self.compile(RESOURCE+ENTRY+'{let a=vec2f(3,4);let r=normalize(a);out[0]=bitcast<u32>(r.x);out[1]=bitcast<u32>(r.y);}')
        memory = {(0,0):[0,0]}
        execute_numeric(code,memory,0)
        self.assertAlmostEqual(f32(memory[(0,0)][0]),0.6,places=6)
        self.assertAlmostEqual(f32(memory[(0,0)][1]),0.8,places=6)
        for expression in ('normalize(vec2f(0))','sqrt(vec2f(1,-1))','length(vec2f(3e38))'):
            self.compile('const r='+expression+';'+ENTRY+'{}','constant')

    def test_invalid_signatures_and_skipped_bounds(self):
        expressions = ['dot(1,2)','normalize(1.0)','cross(vec2f(1),vec2f(2))',
            'cross(vec4f(1),vec4f(2))','length(vec2i(1))','min(vec2i(1),vec2u(2))',
            'distance(1.0,vec2f(1))','min(true,false)','max(array(1,2),array(3,4))',
            'sqrt(vec2f(1),vec2f(2))','dot()','clamp(vec2i(1),vec3i(2),vec2i(3))',
            'dot<f32>(vec2f(1),vec2f(1))']
        for expression in expressions:
            self.compile(ENTRY+'{let r='+expression+';}','category=4')
        self.compile(ENTRY+'{dot(vec2i(1),vec2i(2));}','used result')
        for lower,upper in [('vec2i(0,4)','vec2i(1,3)'),('vec3i(0,0,5)','vec3i(1,1,4)'),('vec4i(9,0,0,0)','vec4i(8,1,1,1)')]:
            self.compile(ENTRY+'{let x=false&&(clamp('+lower+','+lower+','+upper+').x==0i);}','lower bound')
        self.compile(RESOURCE+ENTRY+'{let r=clamp(vec2u(out[0]),vec2u(5,1),vec2u(4,2));}','lower bound')
        self.execute('let lower=vec2i(5,1);let r=clamp(vec2i(3),lower,vec2i(4,2));out[0]=u32(r.x);out[1]=u32(r.y);',[4,2])

    def test_exact_target_lowering_ledger(self):
        rows = []
        for width in (1,2,3,4):
            for leaf in ('i32','u32','f32'):
                operand = leaf if width == 1 else f'vec{width}<{leaf}>'
                for name, arity in (('min',2),('max',2),('clamp',3)):
                    identifiers = (4 if name == 'clamp' else 2) if width == 1 else ((7 if name == 'clamp' else 4)*width+1)
                    words = (22 if name == 'clamp' else 11) if width == 1 else ((38 if name == 'clamp' else 22)*width+3)
                    rows.append((name,arity,operand,operand,identifiers,words))
                if leaf == 'f32':
                    for name,arity,result in (('exp',1,operand),('tanh',1,operand),('sqrt',1,operand),('length',1,'f32'),('distance',2,'f32')):
                        rows.append((name,arity,operand,result,1,5+arity))
                    if width > 1:
                        rows.append(('normalize',1,operand,operand,1,6))
                    if width == 3:
                        rows.append(('cross',2,operand,operand,1,7))
                if width > 1:
                    rows.append(('dot',2,operand,leaf,1 if leaf == 'f32' else 2*width,5 if leaf == 'f32' else 10*width))
        for name,arity,operand,result,identifiers,words in rows:
            formals = ','.join(f'a{i}:{operand}' for i in range(arity))
            actuals = ','.join(f'a{i}' for i in range(arity))
            with self.subTest(name=name, operand=operand):
                code = self.compile(f'fn chosen({formals})->{result}'+'{return '+name+'('+actuals+');}'+ENTRY+'{}')
                selected = []
                active = False
                for opcode,arguments in code:
                    if opcode == 54:
                        active = True
                    elif active and opcode == 254:
                        break
                    elif active and opcode not in (55,248):
                        selected.append((opcode,arguments))
                self.assertEqual(len(selected),identifiers)
                self.assertEqual(sum(len(arguments)+1 for _,arguments in selected),words)
        self.assertEqual(len(rows),69)

    def test_template_rejection_and_comparison_neighbors(self):
        # These single-type calls previously died as comparison parse errors.
        # They now reach the normal callee's semantic type-argument rejection.
        for call in ('dot<f32>(vec2f(1),vec2f(2))',
                     'dot<vec2<f32>>(vec2f(1),vec2f(2))'):
            self.compile(ENTRY+'{let r='+call+';}', 'category=4')
        self.compile('fn dot(x:u32)->u32{return x;}'+ENTRY+'{let x=dot<u32>(1u);}',
                     'invalid function call')
        self.compile('fn dot(x:u32)->u32{return x;}'+ENTRY+'{dot<u32>(1u);}',
                     'invalid function call')
        self.compile('struct dot{x:u32,}'+ENTRY+'{let x=dot<u32>(1u);}',
                     'nominal constructor cannot have type arguments')
        for body, expected in [('out[0]=u32(1u<2u);',[1]),
                               ('out[0]=u32((1u<2u)==true);',[1]),
                               ('let a=1u;let b=2u;let c=3u;out[0]=u32((a<b)&&(b<c));',[1])]:
            self.execute(body, expected)
        for count, error in ((31, 'category=4'), (32, 'depth capacity')):
            annotation = 'vec2<'*count + 'f32' + '>'*count
            self.compile(ENTRY+'{let x=dot<'+annotation+'>(vec2f(1),vec2f(1));}', error)

    def test_names_and_uniformity(self):
        for name in ('dot','length','distance','normalize','cross'):
            self.execute(f'out[0]={name}(7u);',[9],f'fn {name}(x:u32)->u32{{return x+2u;}}')
            self.execute(f'const x={name}(7u);out[0]=x.value;',[7],f'struct {name}{{value:u32,}}')
            self.compile(RESOURCE+f'struct {name}{{value:u32,}}'+ENTRY+f'{{let x={name}(out[0]);out[1]=x.value;}}')
            self.compile(f'const {name}=1;'+ENTRY+f'{{let x={name}(1);}}','module value')
            self.compile(ENTRY+f'{{let {name}=1;let x={name}(1);}}','lexical value')
        entry='@compute @workgroup_size(4) fn main(@builtin(local_invocation_index) lane:u32)'
        self.compile(entry+'{if(dot(vec2u(lane),vec2u(0))==0u){workgroupBarrier();}}','uniform control cannot be established')
        self.compile(entry+'{if(false&&(length(vec2f(f32(lane)))>0.0)){workgroupBarrier();}}','uniform control cannot be established')
        self.compile(entry+'{if(dot(vec2u(1),vec2u(0))==0u){workgroupBarrier();}}')


if __name__ == '__main__':
    unittest.main()
