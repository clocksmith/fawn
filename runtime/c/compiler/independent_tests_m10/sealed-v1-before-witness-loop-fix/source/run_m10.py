#!/usr/bin/env python3
"""Opt-in candidate evaluation. Do not invoke --library before authorized handoff."""
from __future__ import annotations
import argparse,hashlib,json,os,struct,subprocess,tempfile,time
from pathlib import Path
from fixtures_m10 import all_cases,inventory
from extra_m10 import extras,witnesses
from abi_m10 import Compiler,read_reflection
from target_m10 import Module
from seal_m10 import verify_seal

def cases():return all_cases()+extras()+witnesses()

def expected_buffers(case):
    out={k:bytearray(v) for k,v in case['buffers'].items()}
    for key,writes in case['writes'].items():
        for index,value in writes.items():out[key][index]=value
    return out

def execute(case,words):
    buffers={k:bytearray(v) for k,v in case['buffers'].items()};wanted=expected_buffers(case)
    readonly={ (r['group'],r['binding']) for r in case['resources'] if r['access']==1 or r['kind']==2 }
    for r in case['resources']:
        key=r['group'],r['binding']
        if len(buffers[key])<case['schema'].minimum(r['type']) and 'logical_short_binding' not in case['tags']:
            raise AssertionError('independent host minimum admission failed')
    module=Module(words);steps=0;ops={}
    for gid in case['invocations']:
        local=tuple(gid[i]%case['local'][i] for i in range(3));work=tuple(gid[i]//case['local'][i] for i in range(3))
        result=module.execute(buffers,{28:tuple(gid),27:local,26:work,24:(1,1,1),29:local[0]+case['local'][0]*(local[1]+case['local'][1]*local[2])},readonly)
        steps+=result['steps']
        for k,v in result['opcounts'].items():ops[k]=ops.get(k,0)+v
    for key in wanted:
        if buffers[key]!=wanted[key]:
            differences=[(i,buffers[key][i],wanted[key][i]) for i in range(len(wanted[key])) if buffers[key][i]!=wanted[key][i]]
            raise AssertionError(('byte mismatch',key,differences[:12]))
    return dict(steps=steps,opcounts=ops,logical_short_binding='logical_short_binding' in case['tags'])

def evaluate(compiler,case,validator=None):
    row=dict(name=case['name'],expected_status=case['status'])
    if case['status']:
        checks=[]
        for flags in (1,2,3,5,6,7):
            status,result,arrays=compiler.call(case['source'],flags)
            if status!=case['status']:raise AssertionError(('status',flags,status,case['status'],bytes(result.diagnostic.message)))
            if result.required_is_exact or any(result.required.fields().values()) or any(result.published.fields().values()):raise AssertionError('failure counts')
            if result.diagnostic.offset>len(case['source'].encode()):raise AssertionError('diagnostic offset')
            checks.append(status)
        row['flags_status']=checks;return row
    status,measure,_=compiler.call(case['source'],7)
    if status:raise AssertionError(('measure',status,measure.exhausted_pool,measure.minimum_pool_capacity,bytes(measure.diagnostic.message)))
    if measure.required_is_exact!=1 or any(measure.published.fields().values()):raise AssertionError('measure counts')
    counts=measure.required.fields();status,result,arrays=compiler.call(case['source'],3,counts)
    if status:raise AssertionError(('publish',status,bytes(result.diagnostic.message)))
    if result.required.fields()!=counts or result.published.fields()!=counts:raise AssertionError('measure exactness')
    actual=read_reflection(result,arrays)
    if actual!=case['reflection']:raise AssertionError(('typed reflection',actual,case['reflection']))
    words=list(arrays['words']);raw=struct.pack('<'+'I'*len(words),*words)
    row.update(counts=counts,word_sha256=hashlib.sha256(raw).hexdigest(),reflection_sha256=hashlib.sha256(json.dumps(actual,sort_keys=True).encode()).hexdigest())
    if validator:
        with tempfile.TemporaryDirectory(prefix='doe-m10-target-',dir='/tmp') as tmp:
            p=Path(tmp)/'module.spv';p.write_bytes(raw)
            checked=subprocess.run([validator,'--target-env','vulkan1.1',str(p)],capture_output=True,text=True)
            if checked.returncode:raise AssertionError(('spirv-val',checked.stdout,checked.stderr))
        row['validator']='passed'
    else:row['validator']='unrun'
    if case['execute']:row['execution']=execute(case,words)
    return row

def main():
    p=argparse.ArgumentParser();p.add_argument('--inventory',action='store_true');p.add_argument('--library');p.add_argument('--validator');p.add_argument('--select');p.add_argument('--receipt');a=p.parse_args()
    fixtures=cases()
    if a.select:fixtures=[c for c in fixtures if a.select in c['name']]
    if a.inventory:print(json.dumps(inventory(fixtures),sort_keys=True,indent=2));return
    if not a.library:p.error('--library is mandatory for explicit candidate handoff; --inventory uses no candidate')
    verify_seal()
    if a.receipt and Path(a.receipt).exists():p.error('fresh absent receipt required')
    compiler=Compiler(Path(a.library).resolve());rows=[];failed=[]
    for case in fixtures:
        try:row=evaluate(compiler,case,a.validator);row['outcome']='passed'
        except Exception as e:row=dict(name=case['name'],outcome='failed',reason=repr(e));failed.append(case['name'])
        rows.append(row)
    report=dict(format='independent-m10-v1',library_sha256=hashlib.sha256(Path(a.library).read_bytes()).hexdigest(),total=len(rows),passed=len(rows)-len(failed),failed=failed,physical_device='unrun',cases=rows)
    text=json.dumps(report,sort_keys=True,indent=2)+'\n'
    if a.receipt:Path(a.receipt).write_text(text)
    else:print(text)
    raise SystemExit(bool(failed))
if __name__=='__main__':main()
