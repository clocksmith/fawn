"""Opt-in immutable-source M11 evaluation. Never import candidate source code."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from oracle.abi_m10 import Compiler, read_reflection
from oracle.target_m11 import Module
from oracle.workgroup_vm import dispatch, Race

HERE=Path(__file__).resolve().parent
FROZEN=HERE/'frozen'

def verify():
    for line in (HERE/'CASE_SEAL.sha256').read_text().splitlines():
        expected,path=line.split(None,1)
        if hashlib.sha256((HERE/path).read_bytes()).hexdigest()!=expected:
            raise AssertionError('frozen input changed '+path)


def audit_target(module,source):
    workgroup={rid:(ptr,init) for rid,(ptr,storage,init) in module.variables.items() if storage==4}
    expected=source.count('var<workgroup>')
    if len(workgroup)!=expected:raise AssertionError(('workgroup root count',len(workgroup),expected))
    for rid,(ptr,init) in workgroup.items():
        if module.types[ptr][1][0]!=4 or init is not None:raise AssertionError('workgroup pointer/init target ownership')
        if any(k in module.decor.get(rid,{}) for k in (33,34)):raise AssertionError('descriptor masquerading root')
    for op,args,offset in module.ins:
        if op==224 and tuple(module.constants[x] for x in args)!=(2,2,0x108):raise AssertionError('barrier exact scopes/semantics')
    if workgroup:
        body=module.functions[module.entry]
        first=next((i for i,(op,a,p) in enumerate(body) if op==224),None)
        if first is None:raise AssertionError('missing prologue barrier')
        prefix=body[:first]
        if any(op in (57,253,254) for op,a,p in prefix):raise AssertionError('source call/return before initialization')
        stores=[a for op,a,p in prefix if op==62]
        if len(stores)!=len(workgroup) or {a[0] for a in stores}!=set(workgroup):raise AssertionError('one whole-root init store required')
        for root,value in stores:
            op,args,p=module.defs[value]
            if op!=46 or args[0]!=module.types[workgroup[root][0]][1][1]:raise AssertionError('whole typed OpConstantNull required')
        branches=[a for op,a,p in prefix if op==250]
        if len(branches)!=1:raise AssertionError('single lane0 prologue selection')
        cond=module.defs[branches[0][0]]
        if cond[0]!=170:raise AssertionError('lane0 prologue equality')
        operands=cond[1][2:]
        loaded=None
        for operand in operands:
            definition=module.defs[operand]
            if definition[0]==61 and module.inputs.get(definition[1][2])==29:loaded=operand
        if loaded is None or not any(module.constants.get(x)==0 for x in operands):raise AssertionError('lane0 must select LocalInvocationIndex==0')
    return {'workgroup_roots':len(workgroup),'static_barriers':sum(op==224 for op,a,p in module.ins),'input_variables':len(module.inputs),'target_bound':module.bound,'word_count':len(module.words)}


def evaluate(compiler,case,out,validator,execute=True,only_schedule=None):
    source=(FROZEN/case['source']).read_text();row={'id':case['id'],'expected_status':case['expected_status']}
    if case['expected_status']:
        details=[]
        for flags in (1,2,3,5,6,7):
            status,result,arrays=compiler.call(source,flags)
            details.append({'flags':flags,'status':status,'diagnostic':bytes(result.diagnostic.message).decode(),'offset':result.diagnostic.offset})
            if status!=case['expected_status']:raise AssertionError(('wrong status',details))
            if any(result.published.fields().values()):raise AssertionError('partial failure publication')
            if result.diagnostic.offset>len(source.encode()):raise AssertionError('diagnostic outside source')
            if case.get('graph_reaches_nonuniform') and 'uniform' not in bytes(result.diagnostic.message).decode().lower():raise AssertionError(('missing uniformity diagnostic',details))
        row['modes']=details;return row
    status,measure,_=compiler.call(source,7)
    if status:raise AssertionError(('measure failure',status,bytes(measure.diagnostic.message).decode()))
    counts=measure.required.fields()
    if not measure.required_is_exact or any(measure.published.fields().values()):raise AssertionError('measure publication/exactness')
    status,result,arrays=compiler.call(source,3,counts)
    if status:raise AssertionError(('publish failure',status,bytes(result.diagnostic.message).decode()))
    if result.published.fields()!=counts or result.required.fields()!=counts:raise AssertionError('count mismatch')
    reflection=read_reflection(result,arrays);words=list(arrays['words']);module=Module(words)
    raw=struct.pack('<'+'I'*len(words),*words);target=out/(case['id']+'.spv');target.write_bytes(raw)
    check=subprocess.run([str(validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True)
    row.update(word_sha256=hashlib.sha256(raw).hexdigest(),counts=counts,target=audit_target(module,source),validator={'returncode':check.returncode,'output':check.stdout+check.stderr})
    if check.returncode:raise AssertionError(('validation',check.stderr))
    # Every declared root is target-only. Reflection remains descriptor closure.
    if len(reflection['resources'])!=len(module.resources):raise AssertionError('resource/root reflection mismatch')
    if execute and 'execution' in case:
        scenario=case['execution'];records=[]
        schedules=scenario.get('schedules',['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7'])
        if only_schedule:schedules=[only_schedule]
        for schedule in schedules:
            for repeat in range(scenario.get('dispatches',2)):
                buffers={tuple(map(int,k.split(':'))):bytearray.fromhex(v) for k,v in scenario.get('inputs_hex',{}).items()}
                quantum=64
                if case['id'].startswith(('execute_','control_')):quantum=1
                try:
                    start=time.monotonic();record=dispatch(module,buffers,tuple(scenario['groups']),schedule,poison_byte=[0xa5,0x5a][repeat%2],pure_quantum=quantum)
                except Race as exc:
                    if 'oracle_expected_failure' not in scenario:raise
                    record={'expected_race_detected':str(exc)}
                else:
                    if 'oracle_expected_failure' in scenario:raise AssertionError('race control survived')
                    for key,expected in scenario['expected_outputs_hex'].items():
                        actual=buffers[tuple(map(int,key.split(':')))].hex()
                        if actual!=expected:
                            diff=[i for i,(x,y) in enumerate(zip(bytes.fromhex(actual),bytes.fromhex(expected))) if x!=y]
                            raise AssertionError(('execution byte mismatch',key,diff[:16],actual[:160],expected[:160]))
                    record['seconds']=time.monotonic()-start
                records.append(record)
        row['execution']=records
    return row


def main():
    p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--select');p.add_argument('--no-execute',action='store_true');p.add_argument('--schedule');a=p.parse_args()
    verify();a.out.mkdir(exist_ok=False,parents=True);compiler=Compiler(a.library);rows=[];failed=[]
    cases=json.loads((FROZEN/'cases.json').read_text())
    if a.select:cases=[c for c in cases if a.select in c['id']]
    for case in cases:
        start=time.monotonic()
        try:row=evaluate(compiler,case,a.out,a.validator,not a.no_execute,a.schedule);row['outcome']='passed'
        except Exception as error:row={'id':case['id'],'outcome':'failed','reason':repr(error)};failed.append(case['id'])
        row['seconds']=time.monotonic()-start;rows.append(row)
        (a.out/'results.json').write_text(json.dumps({'library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'cases':rows,'failed':failed,'pending':len(cases)-len(rows),'physical_device':False},indent=2)+'\n')
        print(row['id'],row['outcome'],row.get('reason',''),flush=True)
    verify();return bool(failed)
if __name__=='__main__':raise SystemExit(main())
