"""Opt-in M12 independent source/target execution; no compiler invocation on import."""
from __future__ import annotations
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from abi_m10 import Compiler, read_reflection
from exact_numeric_m8 import finite_value
from target_m12 import Module
from workgroup_m12 import dispatch

ROOT=Path(__file__).resolve().parent


def verify() -> None:
    for line in (ROOT/'CASE_SEAL.sha256').read_text().splitlines():
        expected,path=line.split(None,1)
        if hashlib.sha256((ROOT/path).read_bytes()).hexdigest()!=expected:
            raise AssertionError('sealed evaluator input changed: '+path)
    for path,expected in json.loads((ROOT/'MODEL_IMPORTS.json').read_text()).items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=expected:
            raise AssertionError('accepted oracle dependency changed: '+path)


def execute(compiler: Compiler, case: dict, validator: Path, out: Path) -> dict:
    source=(ROOT/case['source']).read_text()
    row={'id':case['id'],'expected_status':case['expected_status']}
    if case['expected_status']:
        modes=[]
        for flags in [1,2,3,5,6,7]:
            status,result,arrays=compiler.call(source,flags)
            modes.append({'flags':flags,'status':status,'diagnostic':bytes(result.diagnostic.message).decode(),'offset':result.diagnostic.offset})
            if status!=case['expected_status']:
                raise AssertionError(('status',modes))
            if any(result.published.fields().values()):
                raise AssertionError('partial failure publication')
            if result.diagnostic.offset>len(source.encode()):
                raise AssertionError('diagnostic outside original source')
        row['modes']=modes
        return row
    status,result,_=compiler.call(source,7)
    if status:
        raise AssertionError(('measure',status,bytes(result.diagnostic.message).decode(),result.exhausted_pool))
    if not result.required_is_exact or any(result.published.fields().values()):
        raise AssertionError('measure exactness/publication')
    counts=result.required.fields()
    status,result,arrays=compiler.call(source,3,counts)
    if status:
        raise AssertionError(('compile',status,bytes(result.diagnostic.message).decode()))
    if result.published.fields()!=counts:
        raise AssertionError('output counts do not match prior exact measure')
    words=list(arrays['words'])
    raw=struct.pack('<'+'I'*len(words),*words)
    target=out/(case['id']+'.spv')
    target.write_bytes(raw)
    check=subprocess.run([str(validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True)
    if check.returncode:
        raise AssertionError(('validator',check.returncode,check.stderr))
    module=Module(words)
    forbidden=[a[3] for op,a,p in module.ins if op==12 and a[3] in {43,44,45}]
    if forbidden:
        raise AssertionError(('poison-prone direct Clamp emitted',forbidden))
    read_reflection(result,arrays)
    row.update(words=len(words),target_sha256=hashlib.sha256(raw).hexdigest(),target_bound=module.bound)
    if 'expected_options' not in case and 'runtime_intervals' not in case:
        row['execution']='validation-only control'
        return row
    buffers={(0,1):bytearray(b'\xa5'*(case['output_words']*4))}
    if 'input_words' in case:
        buffers[(0,0)]=bytearray(struct.pack('<'+'I'*len(case['input_words']),*case['input_words']))
    start=time.monotonic()
    record=dispatch(module,buffers,(1,1,1),'round_robin',pure_quantum=1)
    actual=list(struct.unpack('<'+'I'*case['output_words'],buffers[(0,1)]))
    n=len(case.get('expected_options',case.get('runtime_intervals',[])))
    if actual[n:]!=[0xa5a5a5a5]*(len(actual)-n):
        raise AssertionError(('output guard changed',actual))
    if 'expected_options' in case:
        for i,(got,allowed) in enumerate(zip(actual,case['expected_options'])):
            if got not in allowed:
                raise AssertionError(('output mismatch',i,hex(got),[hex(x) for x in allowed]))
    else:
        for i,(got,bounds) in enumerate(zip(actual,case['runtime_intervals'])):
            low,high=map(Fraction,bounds)
            if not low<=finite_value(got)<=high:
                raise AssertionError(('outside WGSL finite interval',i,hex(got),bounds))
    if 'expected_input_words' in case:
        expected=struct.pack('<'+'I'*len(case['expected_input_words']),*case['expected_input_words'])
        if buffers[(0,0)]!=expected:
            raise AssertionError(('argument history or snapshot mismatch',buffers[(0,0)].hex(),expected.hex()))
    elif 'input_words' in case:
        expected=struct.pack('<'+'I'*len(case['input_words']),*case['input_words'])
        if buffers[(0,0)]!=expected:
            raise AssertionError('unexpected read-only input mutation')
    row.update(actual_words=actual,execution=record,execution_seconds=time.monotonic()-start)
    return row


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,required=True,help='Fresh independent source-built candidate library')
    parser.add_argument('--validator',type=Path,required=True,help='Approved installed spirv-val executable')
    parser.add_argument('--out',type=Path,required=True,help='New output directory; never an imported receipt tree')
    parser.add_argument('--select',default='',help='Optional ID substring; partial runs are not full acceptance')
    args=parser.parse_args()
    verify()
    args.out.mkdir(parents=True,exist_ok=False)
    compiler=Compiler(args.library)
    cases=json.loads((ROOT/'cases.json').read_text())['cases']
    results=[]
    for case in cases:
        if args.select and args.select not in case['id']:
            continue
        try:
            row=execute(compiler,case,args.validator,args.out)
        except Exception as error:
            row={'id':case['id'],'passed':False,'error':repr(error)}
        else:
            row['passed']=True
        results.append(row)
        with (args.out/'events.jsonl').open('a') as stream:
            stream.write(json.dumps(row,sort_keys=True)+'\n')
        print(case['id'],row['passed'],row.get('error',''),flush=True)
    (args.out/'results.json').write_text(json.dumps(results,indent=2,sort_keys=True)+'\n')
    return int(any(not x['passed'] for x in results))

if __name__=='__main__':
    raise SystemExit(main())
