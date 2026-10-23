"""Independently fault-test structured execution against hand-authored vectors."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import m2_fixtures
from semantic_checks import run_vectors
from typed_oracle import Module, OracleLimit, ExecutionError, StructuralError


def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--modules',type=Path,required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    cases={name:ex for name,_,ex in m2_fixtures.admitted(args.root)}
    report=[]
    def load(name):
        raw=(args.modules/(name+'.spv')).read_bytes();words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));mod=Module(words);mod.interface(cases[name]);return words,mod
    def check(label,name,mutate,limit=False):
        words,base=load(name)
        trials=run_vectors(base,cases[name]);changed=words.copy();mutate(changed,base)
        assert changed!=words,label
        try:
            mod=Module(changed);mod.interface(cases[name]);run_vectors(mod,cases[name])
        except OracleLimit as exc:
            report.append(dict(case=label,passed=limit,classification='ORACLE_UNSUPPORTED',reason=str(exc),baseline_trials=trials))
        except (ExecutionError,StructuralError,KeyError,IndexError) as exc:
            report.append(dict(case=label,passed=not limit,classification='DEFECT_DETECTED',reason=str(exc),baseline_trials=trials))
        else:
            report.append(dict(case=label,passed=False,classification='DEFECT_ESCAPED',baseline_trials=trials))
    def opcode(old,new,first=False):
        def change(words,mod):
            for op,a,off in mod.instructions:
                if op==old:
                    words[off]=(words[off]&~65535)|new
                    if first:break
        return change
    check('wrong_branch_comparison','comparison_0',opcode(170,171))
    check('removed_early_return','early_then_return',opcode(253,0,True))
    check('removed_both_arms_return','both_arms_return',opcode(253,0))
    check('subtraction_changed_to_addition','independent_indices',opcode(130,128))
    check('wrap_addition_changed_to_subtraction','condition_wrap',opcode(128,130))
    check('underflow_changed_to_addition','condition_underflow',opcode(130,128))
    check('oob_guard_allows_first_invalid','real_neighbor_sum',opcode(176,178))
    def flip_branch(words,mod):
        _,a,off=next(ins for ins in mod.instructions if ins[0]==250)
        words[off+2],words[off+3]=words[off+3],words[off+2]
    check('untaken_arm_executed','untaken_load_has_no_effects',flip_branch)
    def invert_phi(words,mod):
        for op,a,off in mod.instructions:
            if op==245 and len(a)==6:
                words[off+3],words[off+5]=words[off+5],words[off+3]
    check('phi_wrong_predecessor_value','real_neighbor_sum',invert_phi)
    def wrong_length(words,mod):
        ids={key:rid for rid,key in mod.resources.items()}
        for op,a,off in mod.instructions:
            if op==68 and a[2]==ids[(0,1)]:words[off+3]=ids[(0,0)]
    check('output_uses_input_length','real_neighbor_sum',wrong_length)
    check('unknown_opcode_not_invalid','real_neighbor_sum',lambda w,m:w.extend([(1<<16)|60000]),True)
    # The interpreter is independently tested for executing unreachable: replace
    # an actually selected return, not merely parse a dead unreachable block.
    check('reachable_unreachable_detected','return_only',opcode(253,255,True))
    args.out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return int(not all(r['passed'] for r in report))

if __name__=='__main__':raise SystemExit(main())
