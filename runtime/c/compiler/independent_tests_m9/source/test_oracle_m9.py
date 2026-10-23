"""Handwritten independent activation-frame and GLSL operation controls."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
from compute_oracle_m9 import Module
from fixtures_m5 import storage
from spirv_oracle_base import ExecutionError, StructuralError, OracleLimit
from test_oracle_m8 import instruction, controls as previous_controls


def words(entries: list[tuple]) -> list[int]:
    result=[0x07230203,0x00010300,0,4096,0]
    for entry in entries:result.extend(instruction(*entry))
    return result


def prelude(extra: list[tuple], extended: bool=False) -> list[tuple]:
    # Common type IDs: void1 bool2 u32=3 i32=40 f32=41, storage global8.
    return [(17,1)]+([(11,90,0x4c534c47,0x6474732e,0x3035342e,0)] if extended else [])+[
        (14,0,1),(15,5,20,0x6e69616d,0),(16,20,17,1,1,1),
        (71,8,34,0),(71,8,33,0),(71,6,2),(71,5,6,4),(72,6,0,35,0),
        (19,1),(20,2),(21,3,32,0),(21,40,32,1),(22,41,32),
        (29,5,3),(30,6,5),(32,7,12,6),(32,9,12,3),(32,17,7,3),(33,10,1),
        (43,3,11,0),(43,3,12,1),(43,3,13,2),(43,3,14,3),(43,3,15,7),(43,3,16,10),
        *extra,(59,7,8,12)]


def function_module(mutant: str='') -> list[int]:
    entries=prelude([(33,30,3,3),(33,31,3,3,3),(33,32,3),(33,33,1,3)])
    # addten uses fresh local storage; mid keeps a live local across nested calls.
    entries += [(54,3,100,0,30),(55,3,101),(248,102),(59,17,103,7),
        (62,103,101),(61,3,104,103),(128,3,105,104,16),(254,105 if mutant!='wrong_return' else 101),(56,),
        (54,3,110,0,30),(55,3,111),(248,112),(59,17,113,7),(62,113,111),
        (57,3,114,100,111),(57,3,115,100,114),(61,3,116,113),(128,3,117,115,116),(254,117),(56,),
        (54,3,120,0,31),(55,3,121),(55,3,122),(248,123),
        (132,3,124,121,16),(128,3,125,124,122),(254,125),(56,),
        (54,3,130,0,32),(248,131),(65,9,132,8,11,11),(61,3,133,132),
        (128,3,134,133,12),(62,132,134),(254,134),(56,),
        (54,1,140,0,33),(55,3,141),(248,142),(65,9,143,8,11,13),(62,143,141),(253,),(56,)]
    entries += [(54,1,20,0,10),(248,21),(65,9,22,8,11,11),(62,22,11),
        (57,3,23,130),(57,3,24,130),(57,3,25,120,*((24,23) if mutant=='swapped_args' else (23,24))),
        (65,9,26,8,11,12),(62,26,25),(57,3,27,110 if mutant!='wrong_callee' else 100,15),
        (57,1,28,140,27),(57,3,29,110,14),(62,22,29),(253,),(56,)]
    return words(entries)


def wide_module(count: int=17) -> list[int]:
    entries=prelude([(33,30,3,*([3]*count))])
    entries += [(54,3,100,0,30)]+[(55,3,101+i) for i in range(count)]+[
        (248,200),(128,3,201,101,100+count),(254,201),(56,),
        (54,1,20,0,10),(248,21),(57,3,22,100,*([12]*(count-1)),15),
        (65,9,23,8,11,11),(62,23,22),(253,),(56,)]
    return words(entries)


def extended_module(operation: int, operands: list[int], late_select: bool=False, typ: int=41) -> list[int]:
    entries=prelude([(43,typ,100+i,value) for i,value in enumerate(operands)]+[(41,2,200)],True)
    entries += [(54,1,20,0,10),(248,21),(12,typ,210,90,operation,*range(100,100+len(operands)))]
    result=210
    if late_select:
        entries.append((169,typ,211,200,100,210));result=211
    if typ!=3:entries.append((124,3,212,result));result=212
    entries += [(65,9,213,8,11,11),(62,213,result),(253,),(56,)]
    return words(entries)


def controls() -> list[tuple]:
    rows=[(n,w,[want],error) for n,w,want,error in previous_controls()]
    rows += [('frames_original',function_module(),[26,12,34],None),
             ('frames_wrong_return',function_module('wrong_return'),[6,12,14],None),
             ('frames_swap_arguments',function_module('swapped_args'),[26,21,34],None),
             ('frames_wrong_callee',function_module('wrong_callee'),[26,12,17],None),
             ('wide_17',wide_module(),[8],None)]
    # Explicit hardcoded exact/known-rounded values independent of enclosure
    # generation distinguish wrong ExtInst identity and operand selection.
    for name,op,x,want in [('exp_zero',27,0,0x3f800000),('exp_one',27,0x3f800000,0x402df854),
                          ('tanh_zero',21,0,0),('tanh_one',21,0x3f800000,0x3f42f7d6),
                          ('sqrt_four',31,0x40800000,0x40000000),('sqrt_two',31,0x40000000,0x3fb504f3),
                          ('sqrt_negative_zero',31,0x80000000,0x80000000),
                          ('tanh_negative_zero',21,0x80000000,0x80000000)]:
        rows.append((name,extended_module(op,[x]),[want],None))
    for typ,op,values in [(41,43,[0x3f800000,0x40400000,0x40000000]),(3,44,[1,3,2]),(40,45,[1,3,2])]:
        rows.append((f'unsafe_clamp_{typ}',extended_module(op,values,True,typ),None,'unsafe reversed Clamp'))
    return rows


def run_module(module_words: list[int], length: int=3) -> tuple:
    module=Module(module_words)
    module.interface(dict(entry='main',n=[1,1,1],builtins=[],structs=[],bindings=[storage('out',0)]))
    buffers={(0,0):[0xa55a36c9]*length}
    module.execute(buffers,{},set())
    return buffers[(0,0)],module.last_calls


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validator',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    rows=[]
    for name,module_words,want,error in controls():
        path=args.out/(name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(module_words),*module_words))
        proc=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
        row=dict(case=name,validator_returncode=proc.returncode,validator_stderr=proc.stderr)
        try:
            if proc.returncode:raise AssertionError('handwritten module not Vulkan1.1 valid')
            actual,calls=run_module(module_words,len(want) if want is not None else 1)
            if error:raise AssertionError('dangerous operation escaped before later select')
            if actual!=want:raise AssertionError(f'{actual} != {want}')
            row.update(passed=True,result=actual,call_depth=max((c[3] for c in calls),default=0))
        except ExecutionError as exc:
            row.update(passed=bool(error and error in str(exc)),message=str(exc),detection='instruction_execution')
        except (AssertionError,ValueError,KeyError,IndexError,StructuralError,OracleLimit) as exc:
            row.update(passed=False,message=str(exc))
        rows.append(row)
    (args.out/'report.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
    print(json.dumps(dict(total=len(rows),passed=sum(r['passed'] for r in rows),failures=[r for r in rows if not r['passed']]),indent=2))
    return int(any(not r['passed'] for r in rows))


if __name__=='__main__':raise SystemExit(main())
