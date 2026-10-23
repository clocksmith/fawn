"""Handwritten M8 instruction controls independent of all compiler output."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct
import subprocess
from compute_oracle_m8 import Module
from fixtures_m5 import storage
from spirv_oracle_base import ExecutionError


def instruction(op: int,*args: int) -> list[int]:
    return [((len(args)+1)<<16)|op,*args]


def module_words(opcode: int, left: int, right: int=0,
                 input_type: int=3, output_type: int=3,
                 unary: bool=False, late_select: bool=False,
                 early_safe_operand: bool=False) -> list[int]:
    # Types: bool2, u32=3, i32=40, f32=41. Constant values are raw32 payloads.
    entries=[(17,1),(14,0,1),(15,5,20,0x6e69616d,0),(16,20,17,1,1,1),
        (71,8,34,0),(71,8,33,0),(71,6,2),(71,5,6,4),(72,6,0,35,0),
        (19,1),(20,2),(21,3,32,0),(21,40,32,1),(22,41,32),
        (29,5,3),(30,6,5),(32,7,12,6),(32,9,12,3),(33,10,1),
        (43,3,11,0),(43,3,12,1),(42,2,13),(41,2,14),
        (43,input_type,42,left),(43,3 if opcode in {194,195,196} else input_type,43,right),
        (43,input_type,44,0),(43,input_type,45,0x3f800000 if input_type==41 else 1),
        (59,7,8,12),(54,1,20,0,10),(248,21)]
    operand=42
    rhs=43
    if early_safe_operand:
        # Replace the potentially dangerous operand with safe 1 (division) or0 (conversion).
        target=44 if unary else 45
        typ=input_type if unary else (3 if opcode in {194,195,196} else input_type)
        entries.append((169,typ,46,14,target,42 if unary else 43))
        if unary:operand=46
        else:rhs=46
    entries.append((opcode,output_type,47,operand) if unary else (opcode,output_type,47,operand,rhs))
    result=47
    if output_type!=3:
        entries.append((169,3,48,47,12,11) if output_type==2 else (124,3,48,47))
        result=48
    if late_select:
        entries.append((169,3,49,13,result,11));result=49
    entries += [(65,9,50,8,11,11),(62,50,result),(253,),(56,)]
    words=[0x07230203,0x00010300,0,64,0]
    for entry in entries:words.extend(instruction(*entry))
    return words


def run_module(words: list[int]) -> int:
    module=Module(words)
    module.interface(dict(entry='main',n=[1,1,1],builtins=[],structs=[],bindings=[storage('out',0)]))
    buffers={(0,0):[0xa55a36c9]}
    module.execute(buffers,{},set())
    return buffers[(0,0)][0]


def controls() -> list[tuple]:
    rows=[]
    valid=[('signed_division',135,0xfffffff9,3,40,40,False,0xfffffffe),
           ('signed_remainder',138,0xfffffff9,3,40,40,False,0xffffffff),
           ('signed_modulus_differs',139,0xfffffff9,3,40,40,False,2),
           ('unsigned_division',134,0xfffffff9,3,3,3,False,1431655763),
           ('unsigned_remainder',137,17,3,3,3,False,2),
           ('arithmetic_shift',195,0x80000000,31,40,40,False,0xffffffff),
           ('logical_shift',194,0x80000000,31,3,3,False,1),
           ('shift_left',196,0xffffffff,31,3,3,False,0x80000000),
           ('signed_less',177,0xffffffff,0,40,2,False,1),
           ('unsigned_less',176,0xffffffff,0,3,2,False,0),
           ('integer_negate_min',126,0x80000000,0,40,40,True,0x80000000),
           ('float_negate_zero',127,0,0,41,41,True,0x80000000),
           ('float_remainder',140,0xc0f00000,0x40000000,41,41,False,0xbfc00000),
           ('float_mod_differs',141,0xc0f00000,0x40000000,41,41,False,0x3f000000),
           ('float_division',136,0x3f800000,0x41200000,41,41,False,0x3dcccccd),
           ('float_to_signed',110,0xbff33333,0,41,40,True,0xffffffff),
           ('float_to_unsigned',109,0x3ff33333,0,41,3,True,1),
           ('signed_to_float',111,0xffffffff,0,40,41,True,0xbf800000),
           ('unsigned_to_float',112,0xffffffff,0,3,41,True,0x4f800000),
           ('bitcast_nan_payload',124,0x7fa12345,0,41,3,True,0x7fa12345),
           ('bitwise_and',199,0x12345678,0x55555555,3,3,False,0x10145450)]
    for name,op,a,b,itype,otype,unary,want in valid:
        rows.append((name,module_words(op,a,b,itype,otype,unary),want,None))
    for name,op,a,b,itype,otype,unary in [
        ('late_select_division_zero',134,7,0,3,3,False),
        ('late_select_signed_division_min',135,0x80000000,0xffffffff,40,40,False),
        ('late_select_signed_remainder_min',138,0x80000000,0xffffffff,40,40,False),
        ('late_select_remainder_zero',137,7,0,3,3,False),
        ('late_select_shift32',196,1,32,3,3,False),
        ('late_select_convert_nan',109,0x7fc00001,0,41,3,True),
        ('late_select_convert_inf',110,0x7f800000,0,41,40,True),
        ('late_select_convert_overflow',109,0x4f800000,0,41,3,True)]:
        rows.append((name,module_words(op,a,b,itype,otype,unary,late_select=True),None,'unsafe'))
        if op not in {196}:
            want=0 if unary or op in {137,138} else a
            rows.append((name.replace('late_select','early_safe'),
                         module_words(op,a,b,itype,otype,unary,early_safe_operand=True),want,None))
    return rows


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validator',type=Path,required=True,help='Installed spirv-val.')
    parser.add_argument('--out',type=Path,required=True,help='Fresh evidence directory.')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    rows=[]
    for name,words,want,error in controls():
        path=args.out/(name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
        proc=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
        row=dict(case=name,validator_returncode=proc.returncode,validator_stderr=proc.stderr)
        try:
            if proc.returncode:raise AssertionError('handwritten original/mutation is not valid SPIR-V')
            result=run_module(words)
            if error:raise AssertionError('unsafe operand escaped before later select')
            if result!=want:raise AssertionError(f'{result:#x} != {want:#x}')
            row.update(passed=True,result=result)
        except ExecutionError as exc:
            row.update(passed=bool(error and error in str(exc)),detection='instruction_execution',message=str(exc))
        except (AssertionError,ValueError,KeyError,IndexError) as exc:
            row.update(passed=False,message=str(exc))
        rows.append(row)
    (args.out/'report.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(json.dumps(dict(total=len(rows),passed=sum(r['passed'] for r in rows),failures=[r for r in rows if not r['passed']]),indent=2))
    return int(any(not r['passed'] for r in rows))


if __name__=='__main__':raise SystemExit(main())
