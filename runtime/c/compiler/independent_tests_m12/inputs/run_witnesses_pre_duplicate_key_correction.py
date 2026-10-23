"""Execute complete unchanged witness dispatches, retaining progress and failures."""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import time
from abi_m10 import Compiler
from exact_numeric_m8 import finite_value
from run_cases import verify
from target_m12 import Module
from workgroup_observed import dispatch
from witness_inputs import cases, REPO, ROOT, PATTERN


def stamp():
    return datetime.now(timezone.utc).isoformat()


def verify_supplement():
    for line in (ROOT/'SUPPLEMENT_SEAL.sha256').read_text().splitlines():
        expected,path=line.split(None,1)
        assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==expected,path


def compare(case,buffers):
    for key,original in case['buffers'].items():
        actual=buffers[key]
        if key not in case['outputs']:
            assert actual==original,('input changed',key)
            continue
        expected=case['outputs'][key]
        words=list(struct.unpack('<'+'I'*(len(actual)//4),actual))
        if 'exact_words' in expected:
            assert words==expected['exact_words'],('whole exact output mismatch',key,words[:16],expected['exact_words'][:16])
        else:
            bounds=expected['intervals'];assert words[len(bounds):]==[PATTERN]*expected['guard_words'],('output guard changed',key)
            for index,(got,(low,high)) in enumerate(zip(words,bounds)):
                assert low<=finite_value(got)<=high,('outside propagated finite interval',key,index,hex(got),str(low),str(high))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--select',default='')
    a=p.parse_args();verify();verify_supplement();a.out.mkdir(parents=True,exist_ok=False)
    sealed={row['id']:row for row in json.loads((ROOT/'WITNESS_CASES.json').read_text())['rows']}
    compiler=Compiler(a.library);results=[]
    progress_path=a.out/'progress.jsonl'
    def event(data):
        data['utc']=stamp()
        with progress_path.open('a') as stream:stream.write(json.dumps(data,sort_keys=True)+'\n')
        print(json.dumps(data,sort_keys=True),flush=True)
    for case in cases():
        if a.select and a.select not in case['id']:continue
        row={'id':case['id'],'source':case['source'],'started_at':stamp(),'dispatches':[]}
        target=a.out/(case['id']+'.spv')
        try:
            source=(REPO/case['source']).read_bytes()
            assert hashlib.sha256(source).hexdigest()==sealed[case['id']]['source_sha256']
            for key,value in case['buffers'].items():assert hashlib.sha256(value).hexdigest()==sealed[case['id']]['buffers'][str(key)]['sha256']
            status,result,_=compiler.call(source.decode(),7)
            assert status==0,('compile measure',status,bytes(result.diagnostic.message).decode(),result.exhausted_pool)
            status,result,arrays=compiler.call(source.decode(),3,result.required.fields())
            assert status==0,('compile publish',status,bytes(result.diagnostic.message).decode())
            words=list(arrays['words']);raw=struct.pack('<'+'I'*len(words),*words);target.write_bytes(raw)
            check=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True)
            assert check.returncode==0,('validation',check.stderr)
            row['target_sha256']=hashlib.sha256(raw).hexdigest();row['validated']=True
            module=Module(words)
            shared={}
            for schedule in ['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7']:
                for repeat in range(2):
                    buffers={k:bytearray(v) for k,v in case['buffers'].items()};start=time.monotonic()
                    def report(counters):
                        event(dict(case=case['id'],schedule=schedule,repeat=repeat,elapsed_seconds=time.monotonic()-start,**counters))
                    event(dict(case=case['id'],schedule=schedule,repeat=repeat,state='starting',groups=case['groups'],local_size=module.local))
                    outcome=dispatch(module,buffers,case['groups'],schedule,poison_byte=[0xa5,0x5a][repeat],pure_quantum=64,shared_backing=shared,progress_callback=report)
                    compare(case,buffers)
                    record={'schedule':schedule,'repeat':repeat,'seconds':time.monotonic()-start,'outcome':outcome,'all_buffer_sha256':{str(k):hashlib.sha256(v).hexdigest() for k,v in buffers.items()}}
                    row['dispatches'].append(record)
                    event(dict(case=case['id'],schedule=schedule,repeat=repeat,state='passed',seconds=record['seconds'],**outcome))
                    (a.out/(case['id']+'-partial.json')).write_text(json.dumps(row,indent=2,sort_keys=True)+'\n')
            row['passed']=True;row['executed']=True
        except Exception as error:
            row['passed']=False;row['error']=repr(error)
            event({'case':case['id'],'state':'failed','error':repr(error)})
        row['ended_at']=stamp();results.append(row)
        (a.out/'results.json').write_text(json.dumps({'candidate_library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'candidate_observed':True,'newly_executed':True,'cases':results},indent=2,sort_keys=True)+'\n')
    return int(any(not x['passed'] for x in results))

if __name__=='__main__':raise SystemExit(main())
