#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Doe contributors
"""M7 developer tests through unchanged native CLI input/output options."""
from __future__ import annotations
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from test_cli import ROOT, REPO, VALIDATOR, decode, float_bits
from job_machine import execute_job

HEADER = '@group(0) @binding(0) var<storage,read_write> out:array<u32>; @compute @workgroup_size(1) fn main(){'

class JobTests(unittest.TestCase):
    def compile(self, source: str, expected: int = 0):
        with tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='job-') as directory:
            source_path, output = Path(directory)/'a.wgsl', Path(directory)/'a.spv'
            source_path.write_text(source)
            run = subprocess.run([str(ROOT/'build/doe-compiler-c'), '--shader-path', str(source_path),
                                  '--out', str(output)], capture_output=True)
            if expected:
                self.assertNotEqual(run.returncode, 0, source)
                self.assertIn(f'category={expected}'.encode(), run.stderr, run.stderr.decode())
                self.assertFalse(output.exists())
                return None
            self.assertEqual(run.returncode, 0, run.stderr.decode())
            valid = subprocess.run([str(VALIDATOR), '--target-env', 'vulkan1.1', str(output)], capture_output=True)
            self.assertEqual(valid.returncode, 0, valid.stderr.decode())
            return decode(output.read_bytes())

    def check(self, body, expected, initial=None):
        instructions = self.compile(HEADER + body + '}')
        memory = {(0,0): [99]*len(expected) if initial is None else initial[:]}
        execute_job(instructions, memory, 0)
        self.assertEqual(memory[(0,0)], expected)
        return instructions

    def test_mutable_snapshots_and_scope(self):
        self.check('var x:u32=1; let before=x; x+=4; {var x:u32=7; x++; out[1]=x;} out[0]=before+x;', [6,8])
        self.check('var x:u32=1; {let x=x+1; out[0]=x;} out[1]=x;', [2,1])
        self.check('var x:bool; x=!x; if(x==true){out[0]=3u;}else{out[0]=4u;}', [3])

    def test_for_continue_break_order(self):
        self.check('var sum:u32; for(var i:u32=0;i<9;i++){if(i==2u){continue;} if(i==5u){break;} sum+=i;} out[0]=sum;', [8])
        self.check('var n:u32=0; for(;n<0u;n++){out[0]=0u;} out[0]=n;', [0])
        self.check('var n:u32=0; for(;;){n++; if(n==4u){break;}} out[0]=n;', [4])

    def test_declaration_execution_each_iteration(self):
        self.check('var sum:u32; for(var i:u32=0;i<4u;i++){var x:u32; x+=1u; let snap=i; i+=1u; sum+=x+snap;} out[0]=sum;', [4])
        self.check('var x:u32; while(x<4u){x++;} out[0]=x;', [4])

    def test_continuing_and_nested_loops(self):
        self.check('var n:u32; loop{let snap=n; n++; continuing {out[snap]=n; break if n==3u;}}', [1,2,3])
        self.check('var n:u32; loop{n++; if(n<3u){continue;} break; continuing {out[0]+=10u;}} out[1]=n;', [20,3], [0,0])
        self.check('var n:u32; loop {n++; loop {if(n<3u){break;} return;} continuing {break if n>9u;}} out[0]=7u;', [99])
        self.check('loop{return; continuing{break if true;}} out[0]=3u;', [99])

    def test_short_circuit_and_actual_phi_predecessor(self):
        self.check('var yes:bool=true; if((out[out[9u]]==0u && yes) || out[8u]!=0u){out[0]=7u;}', [7], [0])
        self.check('if(false && out[0]==0u){out[0]=9u;} if(true || out[0]==0u){out[0]=5u;}', [5])
        self.check('var n:u32; while(n<3u && out[8u]==0u){n++;} out[0]=n;', [3])

    def test_robust_compound_wrap(self):
        self.check('var x:u32=4294967295u; x++; out[x]+=out[5u]+1u; out[9u]++; out[1]=0u-1u;', [4,4294967295], [3,0])
        self.check('out[0]=1u+1; out[1]=max(1,2u); if(1<2u){out[2]=3u;} if(1<2){out[3]=4u;}', [2,2,3,4])

    def test_larger_than_legacy_storage(self):
        self.check('var x:u32;'+'x+=1u;'*80+'out[0]=x;', [80])
        self.check('var x:u32;'+'x=1u+2u+3u+4u+5u+6u+7u+8u+9u;'*150+'out[0]=x;', [45])

    def test_static_invalid_and_bounded_unsupported(self):
        semantic = ['loop{}','loop{continue;}','break;','continue;','if(1u){}',
                    'let x=1u;x=2u;','var x:u32;var x:u32;',
                    'loop{continue;let x=1u;continuing{out[0]=x;break if true;}}',
                    'loop{break;continuing{if(false){return;}}}',
                    'loop{break;continuing{break;}}','while true{continuing{}}',
                    'loop{break if true;}','loop{break;continuing{break if true;out[0]=1u;}}',
                    'for(var k:u32=0;k<1u;k=body){let body=2u;}',
                    '{let hidden=1u;}out[0]=hidden;','var b:bool=true;b+=false;',
                    'return;loop{}']
        unsupported = ['var x=1;','let x=(1);','out[2147483648]=1u;',
                       'out[0]=(1-2)+1u;','out[0]=(1+2)+1u;',
                       'var f:f32=1.0+2.0;','if(16777217.0==16777216.0){}']
        for body in semantic:
            with self.subTest(body=body): self.compile(HEADER+body+'}',4)
        for body in unsupported:
            with self.subTest(body=body): self.compile(HEADER+body+'}',3)

    def test_harness_bound_not_shader_semantics(self):
        for body in ('while true{}','loop{if false{break;}}'):
            instructions=self.compile(HEADER+body+'}')
            with self.assertRaisesRegex(AssertionError,'execution unfinished'):
                execute_job(instructions,{(0,0):[]},0,step_limit=250)

    def test_unchanged_real_loop_witnesses(self):
        bits=lambda values:[float_bits(v) for v in values]
        for name in ('dot_product_forward_f32','dot_product_reverse_f32'):
            raw=(REPO/'bench/inference-pipeline/kernels'/f'{name}.wgsl').read_text()
            instructions=self.compile(raw)
            for length in (0,3,5):
                memory={(0,1):bits([1,2,3]),(0,2):bits([4,5,6]),(0,3):bits([99])}
                execute_job(instructions,memory,0,{(0,0):struct.pack('<4I',length,0,0,0)})
                self.assertEqual(memory[(0,3)],bits([0 if length==0 else 32]))
                before={k:v[:] for k,v in memory.items()}
                execute_job(instructions,memory,1,{(0,0):struct.pack('<4I',length,0,0,0)})
                self.assertEqual(memory,before)
        for name in ('matmul_logits_forward_f32','matmul_logits_reverse_f32'):
            raw=(REPO/'bench/inference-pipeline/kernels'/f'{name}.wgsl').read_text()
            instructions=self.compile(raw)
            memory={(0,1):bits([1,2,3]),(0,2):bits([4,5,6,7,8,9]),(0,3):bits([99,99,99])}
            uniforms={(0,0):struct.pack('<4I',2,3,0,0)}
            for row in range(3):
                execute_job(instructions,memory,0,uniforms,inputs={27:(0,0,0),26:(row,0,0)})
            self.assertEqual(memory[(0,3)],bits([32,50,99]))
        raw=(REPO/'bench/kernels/compilation-corpus/moderate_matmul_naive.wgsl').read_text()
        instructions=self.compile(raw)
        memory={(0,0):bits([1,2,3,4,5,6]),(0,1):bits([7,8,9,10,11,12]),(0,2):bits([99]*4)}
        for row in range(3):
            for col in range(3):
                execute_job(instructions,memory,(col,row,0),{(0,3):struct.pack('<3I',2,2,3)})
        self.assertEqual(memory[(0,2)],bits([58,64,139,154]))

    def test_float_conditions(self):
        source=HEADER.replace('array<u32>','array<f32>')+'var f:f32=1; if(f<2.0 && f!=3.0){out[0]=f;} }'
        instructions=self.compile(source); memory={(0,0):[0]}
        execute_job(instructions,memory,0); self.assertEqual(memory[(0,0)],[float_bits(1)])

if __name__=='__main__': unittest.main()
