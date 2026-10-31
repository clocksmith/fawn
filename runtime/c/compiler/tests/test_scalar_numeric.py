#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""M8 owner developer tests; historical evaluator/fixtures remain untouched."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_cli import ROOT, REPO, VALIDATOR, decode, float_bits
from scalar_machine import execute_scalar

HEADER = '@group(0) @binding(0) var<storage,read_write> out:array<u32>; @compute @workgroup_size(1) fn main(){'

class ScalarTests(unittest.TestCase):
    def compile(self, source, error=None, fixed=False):
        with tempfile.TemporaryDirectory(dir=ROOT/'build', prefix='scalar-') as directory:
            path, output = Path(directory)/'a.wgsl', Path(directory)/'a.spv'
            path.write_text(source)
            command=[str(ROOT/'build/doe-compiler-c'),'--shader-path',str(path),'--out',str(output)]
            if fixed: command += ['--reflection',str(Path(directory)/'a.json')]
            run=subprocess.run(command,capture_output=True)
            if error is not None:
                self.assertNotEqual(run.returncode,0,source)
                self.assertIn(f'category={error}'.encode(),run.stderr,run.stderr.decode())
                self.assertFalse(output.exists())
                return
            self.assertEqual(run.returncode,0,run.stderr.decode())
            valid=subprocess.run([str(VALIDATOR),'--target-env','vulkan1.1',str(output)],capture_output=True)
            self.assertEqual(valid.returncode,0,valid.stderr.decode())
            return decode(output.read_bytes())
    def check(self, body, expected, initial=None, prefix='', fixed=False):
        code=self.compile(prefix+HEADER+body+'}',fixed=fixed)
        memory={(0,0):list(initial if initial is not None else [99]*len(expected))}
        execute_scalar(code,memory,0)
        self.assertEqual(memory[(0,0)],expected)
        return code
    def test_const_and_materialization(self):
        for fixed in (False,True):
            self.check('const a=16777217.0; const b=a-16777216.0; out[0]=u32(b); out[1]=u32(4294967296.0); out[2]=u32(4294967296.0f);',[1,4294967295,4294967040],fixed=fixed)
            self.check('const n=6; const m=n*7; let x:i32=m; out[0]=u32(x); out[1]=u32(f32(4294967295u));',[42,4294967040],fixed=fixed)
            self.check('out[0]=bitcast<u32>(4294967295);',[4294967295],fixed=fixed)
            self.check('out[0]=A;',[17],prefix='const A:u32=B+1;const B=16;',fixed=fixed)
            self.check('let x=0x1.000001p0; const y=0x1.000001p0; let z:f32=y; out[0]=bitcast<u32>(x);out[1]=bitcast<u32>(z);',[0x3f800000]*2,fixed=fixed)
    def test_decimal_finite_boundary(self):
        for fixed in (False, True):
            # Exact ONNX MaxPool initializer; sign belongs to unary evaluation.
            self.check('var value=-3.4028234663852886e+38f;out[0]=bitcast<u32>(value);'
                       'out[1]=bitcast<u32>(3.4028235677973366163e38f);',
                       [0xff7fffff, 0x7f7fffff], fixed=fixed)
            for literal in ('3.4028235677973366164e38f', '-3.4028235677973366164e38f',
                            '0x1.fffffe1p127f'):
                self.compile(HEADER + 'out[0]=bitcast<u32>(' + literal + ');}', 4, fixed)
    def test_runtime_integer(self):
        self.check('let x=out[0];let z=out[1];out[2]=x/z;out[3]=x%z;out[4]=x<<z;out[5]=x>>z;',[0x87654321,0,0x87654321,0,0x87654321,0x87654321], [0x87654321,0,99,99,99,99])
        self.check('let x=bitcast<i32>(out[0]);let y=bitcast<i32>(out[1]);out[2]=bitcast<u32>(x/y);out[3]=bitcast<u32>(x%y);out[4]=bitcast<u32>(x>>(32u-1u));',[0x80000000,0xffffffff,0x80000000,0,0xffffffff],[0x80000000,0xffffffff,0,0,0])
    def test_runtime_shift_bits(self):
        self.check('let count=1u;let x=1<<count;out[0]=u32(x);',[2])
        self.compile(HEADER+'let count=1u;let x:u32=1<<count;}',4)
        self.compile(HEADER+'let count=1u;let x=4294967295<<count;}',4)
        self.check('let x=out[0];let n=out[1];out[2]=x<<n;out[3]=x>>n;out[4]=~x;out[5]=x^0x1234u;',[0x87654321,33,0x0eca8642,0x43b2a190,0x789abcde,0x87655115],[0x87654321,33,0,0,0,0])
        self.check('var a:i32=-7;var b:i32=3;a/=b;out[0]=bitcast<u32>(a);a=-7;a%=b;out[1]=bitcast<u32>(a);',[0xfffffffe,0xffffffff])
    def test_runtime_conversion(self):
        for bits, unsigned, signed in [(0x7f800000,4294967040,2147483520),(0xff800000,0,0x80000000),(0x7fc12345,0,0),(float_bits(-2.75),0,0xfffffffe),(float_bits(42.75),42,42)]:
            self.check('let bits=out[0];let x=bitcast<f32>(bits);out[1]=u32(x);out[2]=bitcast<u32>(i32(x));out[3]=u32(bool(x));',[bits,unsigned,signed,1],[bits,0,0,0])
        self.check('let x=out[0];let b=bool(x);out[1]=u32(b);out[2]=bitcast<u32>(f32(b));',[1,1,0x3f800000],[1,0,0])
    def test_short_circuit_and_invalid(self):
        self.check('const ok=false && (10i<i32(5*1000*1000*1000)); out[0]=u32(ok);',[0])
        self.check('let bits=0x7f800000u;let f=bitcast<f32>(bits);out[0]=bitcast<u32>(f);',[0x7f800000])
        invalid=['const bad=true && (10i<i32(5*1000*1000*1000));','let f=bitcast<f32>(0x7f800000u);','out[0]=out[1]/0u;','out[0]=out[1]<<32u;','if(false && (1u/0u==0u)){}','let x=f32(4294967295);','let x=0x1.000001p0f;','out[-1]=1u;','let x=true^false;','let u32=3u;let x=u32(4);']
        for body in invalid:
            with self.subTest(body=body):self.compile(HEADER+body+'}',4)
        for prefix in ('const A=B;const B=A;','const A=false && B;const B=A;','const A=missing;','const A=1u/0u;'):
            self.compile(prefix+HEADER+'}',4)
    def test_grammar(self):
        for body in ['out[0]=1u+2u&3u;','out[0]=1u|2u^3u;','out[0]=1u<<2u+1u;','out[0]=1u<<2u<<1u;']:
            self.compile(HEADER+body+'}',2)
        self.check('out[0]=(1u+2u)&3u;out[1]=(1u|2u)^3u;out[2]=1u<<(2u+1u);',[3,0,8])
    def test_scope_attributes_and_signed_robust(self):
        self.check('const x=2;{const x=3;out[0]=u32(x);}out[1]=u32(x);',[3,2])
        # i=-1 clamps to the last word (compiler/src/spirv_clamp.c): out[1]=out[1], then 7.
        self.check('let i=bitcast<i32>(out[0]);out[1]=out[i];out[i]=7u;',[0xffffffff,7],[0xffffffff,99])
        source='@group(G) @binding(B) var<storage,read_write> out:array<u32>;@compute @workgroup_size(X,2) fn main(){out[0]=5u;}const G=0;const B=0;const X=128;'
        code=self.compile(source);memory={(0,0):[99]};execute_scalar(code,memory,0);self.assertEqual(memory[(0,0)],[5])
    def test_unchanged_six_witnesses(self):
        for name in ('bench/kernels/package_image_rgba_invert.wgsl','bench/kernels/matrix_vector_mul_32768x2048_f32.wgsl','bench/kernels/compilation-corpus/moderate_rope.wgsl','bench/inference-pipeline/kernels/rope.wgsl','bench/inference-pipeline/kernels/attention_small_f16kv.wgsl','bench/inference-pipeline/kernels/attention_decode_online_f16kv.wgsl'):
            with self.subTest(path=name):self.compile((REPO/name).read_text())

if __name__=='__main__':unittest.main()
