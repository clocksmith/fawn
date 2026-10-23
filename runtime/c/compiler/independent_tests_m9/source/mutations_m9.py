"""Predeclared emitted-module mutations; valid target variants must misexecute."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
from compute_oracle_m9 import Module
from semantic_m9 import cases,execute_vectors
from semantic_m8 import write_json
from spirv_oracle_base import ExecutionError,OracleLimit,StructuralError

# The mutation policies are pre-candidate, not tuned to observed emission.
PLANS=[('argument_order_snapshots','swap_call_args'),
       ('nested_argument_segments','swap_call_args'),
       ('calls_preserve_live_caller','return_parameter'),
       ('post_call_resource_reload','omit_helper_store'),
       ('no_result','omit_entry_store'),
       ('runtime_exp_f32','wrong_math'),('runtime_tanh_f32','wrong_math'),('runtime_sqrt_f32','wrong_math'),
       ('constant_exp_f32_9','wrong_constant'),
       ('runtime_u32_min','invert_order'),('runtime_i32_max','invert_order'),('runtime_f32_clamp','invert_order')]


def mutate(words: list[int],policy: str,expected: dict) -> tuple[list[int],str]:
    module=Module(words);module.interface(expected);result=list(words)
    if policy=='swap_call_args':
        candidates=[(a,o) for op,a,o in module.instructions if op==57 and len(a)>=5 and a[3]!=a[4]]
        if not candidates:raise ValueError('no distinguishable two-argument call for sealed mutation')
        a,o=candidates[-1];result[o+4],result[o+5]=result[o+5],result[o+4]
        return result,'swapped first two actual argument IDs'
    if policy=='return_parameter':
        for fn,body in module.functions.items():
            parameters=[a[1] for op,a,o in body if op==55]
            if not parameters:continue
            for op,a,o in body:
                if op==254 and a[0]!=parameters[0] and module.defs[a[0]][1][0]==module.defs[parameters[0]][1][0]:
                    result[o+1]=parameters[0];return result,'returned parameter instead of computed value'
        raise ValueError('no typed computed helper return for sealed mutation')
    if policy in {'omit_helper_store','omit_entry_store'}:
        for fn,body in module.functions.items():
            if (fn==module.entry_fn)!=(policy=='omit_entry_store'):continue
            candidates=[(a,o) for op,a,o in body if op==62]
            if candidates:
                a,o=candidates[-1];result[o:o+3]=[0x00010000]*3
                return result,'replaced one store with three valid OpNop instructions'
        raise ValueError('no relevant store for sealed mutation')
    if policy=='wrong_math':
        for op,a,o in module.instructions:
            if op==12 and a[3] in {21,27,31}:
                result[o+4]={21:27,27:31,31:21}[a[3]]
                return result,'changed typed GLSL scalar operation identity'
        raise ValueError('no scalar math instruction')
    if policy=='wrong_constant':
        nearest=expected['numeric_reference']['nearest_bits']
        for op,a,o in module.instructions:
            if op==43 and a[2]==nearest:
                result[o+3]=(nearest+8)&0xffffffff
                return result,'biased folded result by eight representable steps'
        raise ValueError('no independently expected constant payload')
    if policy=='invert_order':
        swaps={172:176,174:178,176:172,178:174,173:177,175:179,177:173,179:175,184:186,186:184,188:190,190:188}
        for op,a,o in module.instructions:
            if op in swaps and a[0] in module.types and module.types[a[0]]==(20,()):
                # Last ordered comparison is selected by deterministic iteration,
                # excluding resource guard unsigned less-than before a math op.
                if op in {172,176}:
                    later=[x for x in module.instructions if x[2]>o]
                    if not any(x[0]==169 and x[1][2]==a[1] for x in later):continue
                result[o]=(result[o]&0xffff0000)|swaps[op]
                return result,'inverted ordered comparison before select'
        raise ValueError('no ordered comparison/select for sealed mutation')
    raise ValueError('unrecognized sealed mutation policy')


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','modules','validator','out']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    known={n:e for n,s,e in cases(args.repo,'all')[0]};rows=[]
    for name,policy in PLANS:
        path=args.modules/(name+'.spv')
        raw=path.read_bytes();words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));expected=known[name]
        original=Module(words);original.interface(expected);execute_vectors(original,expected)
        changed,description=mutate(words,policy,expected)
        output=args.out/(name+'__'+policy+'.spv');output.write_bytes(struct.pack('<'+'I'*len(changed),*changed))
        val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(output)],capture_output=True,text=True,check=False)
        row=dict(case=name,policy=policy,description=description,validator_returncode=val.returncode,validator_stderr=val.stderr,passed=False)
        if not val.returncode:
            try:
                mutant=Module(changed);mutant.interface(expected);execute_vectors(mutant,expected)
            except ExecutionError as exc:row.update(passed=True,detection='actual_instruction_execution',message=str(exc))
            except (OracleLimit,StructuralError,ValueError,KeyError,IndexError,TypeError) as exc:row.update(message='evaluator limitation, not mutant detection: '+str(exc))
        rows.append(row)
    report=dict(total=len(rows),passed=sum(r['passed'] for r in rows),failed=sum(not r['passed'] for r in rows),records=rows)
    write_json(args.out/'results.json',report);print(json.dumps(report,indent=2));return int(bool(report['failed']))


if __name__=='__main__':raise SystemExit(main())
