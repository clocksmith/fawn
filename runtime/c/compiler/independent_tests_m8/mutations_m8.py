"""Validated mutations of actual emitted modules; execution, never validator failure, detects them."""
from __future__ import annotations
import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
from compute_oracle_m8 import Module
from semantic_m8 import source_cases,execute_vectors,sha,write_json
from spirv_oracle_base import ExecutionError,OracleLimit


def instruction(op: int,args: list) -> list:return [((len(args)+1)<<16)|op,*args]


def decode(raw: bytes) -> tuple:
    words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));rows=[];i=5
    while i<len(words):n=words[i]>>16;rows.append([words[i]&65535,words[i+1:i+n]]);i+=n
    return words[:5],rows


def pack(header: list,rows: list) -> bytes:
    words=list(header)
    for op,args in rows:words+=instruction(op,args)
    return struct.pack('<'+'I'*len(words),*words)


def mutate(header: list,rows: list,kind: str) -> bytes:
    defs={args[1]:(op,args) for op,args in rows if op in {109,110,124,169,199,245}}
    if kind.startswith('unguard_'):
        opwanted={'unguard_udiv':134,'unguard_sdiv':135,'unguard_urem':137,'unguard_srem':138}[kind]
        op,args=next(r for r in rows if r[0]==opwanted);producer,select=defs[args[3]]
        if producer!=169:raise ValueError('guard must be pre-instruction select')
        args[3]=select[-1]
    elif kind=='unmask_shift':
        _,args=next(r for r in rows if r[0]==196);op,mask=defs[args[3]]
        if op!=199:raise ValueError('shift count expected independently masked')
        args[3]=mask[2]
    elif kind.startswith('unclamp_'):
        opwanted=109 if kind.endswith('u32') else 110
        _,args=next(r for r in rows if r[0]==opwanted)
        _,raw=next(r for r in rows if r[0]==124 and r[1][0]==4)
        args[2]=raw[1]
    elif kind=='signed_div_unsigned':
        index=next(i for i,r in enumerate(rows) if r[0]==135);a=rows[index][1]
        x,y,q=range(header[3],header[3]+3);header[3]+=3
        rows[index:index+1]=[[124,[3,x,a[2]]],[124,[3,y,a[3]]],[134,[3,q,x,y]],[124,[a[0],a[1],q]]]
    elif kind=='contract_runtime_fma':
        imp,negative=range(header[3],header[3]+2);header[3]+=2
        text=b'GLSL.std.450\0';text+=b'\0'*((-len(text))%4)
        data=list(struct.unpack('<'+'I'*(len(text)//4),text))
        index=next(i for i,r in enumerate(rows) if r[0]==14);rows.insert(index,[11,[imp,*data]])
        index=next(i for i,r in enumerate(rows) if r[0]==54);rows.insert(index,[43,[4,negative,0xbf800000]])
        mul=next(r[1] for r in rows if r[0]==133);sub=next(r for r in rows if r[0]==131)
        sub[0]=12;sub[1]=[4,sub[1][1],imp,50,mul[2],mul[3],negative]
    elif kind in {'signed_shift_logical','signed_compare_unsigned','float_rem_mod'}:
        before,after={'signed_div_unsigned':(135,134),'signed_shift_logical':(195,194),
                      'signed_compare_unsigned':(177,176),'float_rem_mod':(140,141)}[kind]
        row=next(r for r in rows if r[0]==before);row[0]=after
    elif kind=='bitcast_numeric_conversion':
        row=next(r for r in reversed(rows) if r[0]==124 and r[1][0]==3);row[0]=109
    elif kind=='negzero_positive':
        row=next(r for r in rows if r[0]==127);row[0]=83 # OpCopyObject is valid same-type, but oracle knows it.
    elif kind in {'eager_short_rhs','bypass_signed_access'}:
        # Add true constant before function; do not reuse candidate condition values.
        trueid=header[3];header[3]+=1
        index=next(i for i,r in enumerate(rows) if r[0]==54);rows.insert(index,[41,[2,trueid]])
        branches=[r for r in rows if r[0]==250]
        if kind=='eager_short_rhs':branches[1][1][0]=trueid
        else:branches[1][1][0]=trueid
    elif kind=='wrong_phi_predecessor_value':
        row=next(r for r in rows if r[0]==245 and r[1][0]==2)
        # Replace actual incoming RHS with the earlier dominating left value.
        row[1][4]=row[1][2]
    else:raise ValueError(kind)
    return pack(header,rows)


CASES=[('unguard_udiv','runtime_u32_div'),('unguard_sdiv','runtime_i32_div'),
 ('unguard_urem','runtime_u32_rem'),('unguard_srem','runtime_i32_rem'),
 ('unmask_shift','runtime_u32_shl'),('unclamp_u32','convert_f32_to_u32'),('unclamp_i32','convert_f32_to_i32'),
 ('signed_div_unsigned','runtime_i32_div'),('signed_shift_logical','runtime_i32_shr'),
 ('signed_compare_unsigned','runtime_i32_lt'),('float_rem_mod','runtime_f32_rem'),
 ('bitcast_numeric_conversion','bitcast_f32_to_u32'),('negzero_positive','runtime_f32_negate_bits'),
 ('eager_short_rhs','bool_short_and'),('wrong_phi_predecessor_value','bool_short_and'),
 ('bypass_signed_access','signed_index_read'),('contract_runtime_fma','no_runtime_contraction')]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ['repo','scalar-modules','constant-modules','validator','out']:parser.add_argument('--'+n,type=Path,required=True)
    parser.add_argument('--case',action='append',default=[]);args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    known={n:(s,e) for n,s,e in source_cases(args.repo,'all')[0]};records=[]
    for kind,name in CASES:
        if args.case and kind not in args.case:continue
        path=(args.constant_modules if name.startswith('signed_index') else args.scalar_modules)/(name+'.spv')
        raw=path.read_bytes();source,expected=known[name]
        original=Module(list(struct.unpack('<'+'I'*(len(raw)//4),raw)));original.interface(expected);execute_vectors(original,expected)
        header,rows=decode(raw);changed=mutate(header,rows,kind);target=args.out/(kind+'.spv');target.write_bytes(changed)
        val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True,check=False)
        row=dict(control=kind,fixture=name,source_sha256=sha(source),original_sha256=sha(raw),mutant_sha256=sha(changed),
                 validator_returncode=val.returncode,validator_stderr=val.stderr,passed=False)
        if val.returncode:row['reason']='mutant rejected by validator, not execution evidence'
        else:
            try:
                module=Module(list(struct.unpack('<'+'I'*(len(changed)//4),changed)));module.interface(expected);execute_vectors(module,expected)
                row['reason']='mutant escaped independent execution'
            except OracleLimit as e:row['reason']='oracle limitation, not detection: '+str(e)
            except ExecutionError as e:row.update(passed=True,detection='actual_instruction_execution',reason=str(e))
        records.append(row);write_json(args.out/'results.json',records)
        print(json.dumps(row),flush=True)
    print(json.dumps(dict(total=len(records),passed=sum(r['passed'] for r in records))))
    return int(any(not r['passed'] for r in records))


if __name__=='__main__':raise SystemExit(main())
