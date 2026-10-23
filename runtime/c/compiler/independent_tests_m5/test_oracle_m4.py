"""Independent executable instruction mutations for M4; no compiler source imports."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import random
import struct
import m4_fixtures
from rational_oracle import operation, self_test
from semantic_checks import run_vectors
from typed_oracle import Module, OracleLimit, ExecutionError, StructuralError, f32_bits, bits_f32


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['root','modules','out']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();cases={n:ex for n,_,ex in m4_fixtures.admitted(a.root)};report=[];self_test()
    def check(label,name,mutator):
        raw=(a.modules/(name+'.spv')).read_bytes();words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));base=Module(words);base.interface(cases[name]);trials=run_vectors(base,cases[name]);changed=words.copy();mutator(changed,base)
        assert changed!=words,label
        try:
            mod=Module(changed);mod.interface(cases[name]);run_vectors(mod,cases[name])
        except (ExecutionError,StructuralError,KeyError,IndexError) as exc:
            report.append(dict(case=label,passed=True,classification='DEFECT_DETECTED',reason=str(exc),baseline_trials=trials))
        except OracleLimit as exc:
            report.append(dict(case=label,passed=False,classification='UNSUPPORTED_NOT_DEFECT',reason=str(exc),baseline_trials=trials))
        else:report.append(dict(case=label,passed=False,classification='DEFECT_ESCAPED',baseline_trials=trials))
    def opcode(old,new):
        def mutate(w,m):
            for op,args,off in m.instructions:
                if op==old:w[off]=(w[off]&~65535)|new
        return mutate
    check('float_multiply_to_add','fmul',opcode(133,129))
    check('float_subtract_to_add','fsub',opcode(131,129))
    check('u32_multiply_to_add','u32_wrap',opcode(132,128))
    check('float_max_becomes_min','fmax',opcode(186,184))
    check('uint_max_becomes_min','u32_max',opcode(172,176))
    def swap_select(w,m):
        for op,args,off in m.instructions:
            if op==169:w[off+4],w[off+5]=w[off+5],w[off+4]
    check('float_max_select_arms','fmax_reverse',swap_select)
    def bad_decimal(w,m):
        for op,args,off in m.instructions:
            if op==43 and args[2]==0x4b800000:w[off+3]=0x4b800001
    check('decimal_tie_rounded_up','decimal_6_store',bad_decimal)
    def rebound_snapshot(w,m):
        stores=[(args,off) for op,args,off in m.instructions if op==62]
        w[stores[1][1]+2]=stores[0][0][1]
    check('local_snapshot_rebound_after_store','local_snapshot',rebound_snapshot)
    def flip_branch(w,m):
        # First branch is a robust load guard; locate the explicit u32 equality.
        compare={args[1] for op,args,off in m.instructions if op==170}
        for op,args,off in m.instructions:
            if op==250 and args[0] in compare:
                w[off+2],w[off+3]=w[off+3],w[off+2];return
    check('local_branch_wrong_arm','branch_dominance_return',flip_branch)
    def change_phi(w,m):
        for op,args,off in m.instructions:
            if op==245 and len(args)==6:
                w[off+3],w[off+5]=w[off+5],w[off+3]
    check('local_value_wrong_phi_predecessor','branch_dominance_return',change_phi)
    check('robust_mul_first_oob','fmul',opcode(176,178))
    def remove_return(w,m):
        for op,args,off in m.instructions:
            if op==253:w[off]=(w[off]&~65535)|0;return
    check('local_return_removed','both_arms_local_return',remove_return)
    rng=random.Random(0xd0e4004);count=0
    for _ in range(20000):
        left=(rng.getrandbits(1)<<31)|(rng.randrange(90,160)<<23)|rng.getrandbits(23)
        right=(rng.getrandbits(1)<<31)|(rng.randrange(90,160)<<23)|rng.getrandbits(23)
        for op in [131,133]:
            got=operation(op,left,right);x,y=bits_f32(left),bits_f32(right)
            want=f32_bits(x-y if op==131 else x*y)
            assert got==want,(op,hex(left),hex(right),hex(got),hex(want));count+=1
    report.append(dict(case='exact_integer_reference_native_binary32_crosscheck',passed=True,trials=count,note='Finite range only; multiplication of two binary32 significands is exactly representable in binary64. Not the decimal oracle.'))
    a.out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return int(not all(r['passed'] for r in report))
if __name__=='__main__':raise SystemExit(main())
