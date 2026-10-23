"""Exact M10/M11 semantic preservation with two predeclared M12 admission lifts."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
from abi_m10 import Compiler,read_reflection
from model_m10 import Schema,resource
from target_m12 import Module
from workgroup_m12 import dispatch

ROOT=Path(__file__).resolve().parent


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
    owner=a.repo/'runtime/c/compiler';path=owner/'independent_tests_m10/candidate-v3/postdisclosure_semantic_adapter.py';spec=importlib.util.spec_from_file_location('accepted_m10_adapter',path);adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter);adapter.verify_seal();cases,old_changes=adapter.corrected_cases()
    compiler=Compiler(a.library);records=[]
    workgroup=json.loads((owner/'independent_tests_m11/HISTORICAL_MAIN_MAPPING.json').read_text());private=json.loads((owner/'independent_tests_m11/POST_CANDIDATE_PRIVATE_STATUS_MAPPING.json').read_text())
    lift_data=json.loads((ROOT/'EXACT_HISTORICAL_LIFTS.json').read_text());lifts={r['name']:r for r in lift_data['cases']}
    assert len(cases)==576
    for case in cases:
        try:
            lifted=case['name'] in lifts
            if lifted or case['name']=='workgroup':
                mapping=lifts[case['name']] if lifted else workgroup
                assert hashlib.sha256(case['source'].encode()).hexdigest()==mapping['source_sha256']
                assert case['status']==(3 if lifted else 2)
                status,r,_=compiler.call(case['source'],7);assert status==0,(status,bytes(r.diagnostic.message))
                status,r,arrays=compiler.call(case['source'],3,r.required.fields());assert status==0
                schema=Schema();rs=[resource('out',schema.array('u32',None),0,access=2)];assert read_reflection(r,arrays)==schema.reflection(rs)
                words=list(arrays['words']);target=a.out/(case['name']+'.spv');raw=struct.pack('<'+'I'*len(words),*words);target.write_bytes(raw)
                val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True);assert val.returncode==0,val.stderr
                buffers={(0,0):bytearray([0xa7]*12)};proof=dispatch(Module(words),buffers,pure_quantum=1);assert buffers[(0,0)]==bytes([0xa7]*12)
                result={'name':case['name'],'exact_admission_lift':mapping,'execution':proof,'target_sha256':hashlib.sha256(raw).hexdigest()}
            elif case['name']=='private':
                assert hashlib.sha256(case['source'].encode()).hexdigest()==private['source_sha256'] and case['status']==private['accepted_M10_status'];corrected=dict(case);corrected['status']=private['M11_status'];result=adapter.evaluate(compiler,corrected,str(a.validator));result['exact_private_contract_mapping']=private['source_sha256']
            else:
                result=adapter.evaluate(compiler,case,str(a.validator))
            result['outcome']='passed'
        except Exception as error:result={'name':case['name'],'outcome':'failed','reason':repr(error)}
        records.append(result)
        (a.out/'results.json').write_text(json.dumps({'candidate_library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'old_M10_repairs_preserved':old_changes,'new_M12_lifts':lift_data,'cases':records,'failed':[x['name'] for x in records if x['outcome']=='failed']},indent=2,sort_keys=True)+'\n')
        if result['outcome']=='failed':print(result,flush=True)
    return int(any(x['outcome']=='failed' for x in records))

if __name__=='__main__':raise SystemExit(main())
