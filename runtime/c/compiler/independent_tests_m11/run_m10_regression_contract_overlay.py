"""Portable exact M11 contract overlay; original runner and historical tests remain intact."""
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
from target_m11 import Module
from workgroup_vm import dispatch


def main():
    p=argparse.ArgumentParser();p.add_argument('--m10-root',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--select');a=p.parse_args();a.out.mkdir(exist_ok=False)
    path=a.m10_root/'candidate-v3/postdisclosure_semantic_adapter.py';spec=importlib.util.spec_from_file_location('accepted_m10_adapter',path);adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter)
    adapter.verify_seal();cases,old_changes=adapter.corrected_cases();compiler=Compiler(a.library);records=[]
    mapping=json.loads((Path(__file__).resolve().parent/'HISTORICAL_MAIN_MAPPING.json').read_text())
    if a.select:cases=[case for case in cases if case['name']==a.select]
    private_mapping=json.loads((Path(__file__).resolve().parent/'POST_CANDIDATE_PRIVATE_STATUS_MAPPING.json').read_text())
    for case in cases:
        try:
            if case['name']=='workgroup':
                assert hashlib.sha256(case['source'].encode()).hexdigest()==mapping['source_sha256'] and case['status']==2
                status,r,_=compiler.call(case['source'],7);assert status==0,(status,bytes(r.diagnostic.message))
                status,r,arrays=compiler.call(case['source'],3,r.required.fields());assert status==0
                schema=Schema();rs=[resource('out',schema.array('u32',None),0,access=2)]
                assert read_reflection(r,arrays)==schema.reflection(rs)
                words=list(arrays['words']);target=a.out/'lifted-workgroup.spv';target.write_bytes(struct.pack('<'+'I'*len(words),*words))
                checked=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True);assert checked.returncode==0,checked.stderr
                buffer={(0,0):bytearray([0xa7]*12)};proof=dispatch(Module(words),buffer,pure_quantum=1);assert buffer[(0,0)]==bytes([0xa7]*12)
                result={'name':case['name'],'exact_admission_lift':mapping,'execution':proof}
            elif case['name']=='private':
                assert hashlib.sha256(case['source'].encode()).hexdigest()==private_mapping['source_sha256'] and case['status']==private_mapping['accepted_M10_status']
                corrected=dict(case);corrected['status']=private_mapping['M11_status']
                result=adapter.evaluate(compiler,corrected,str(a.validator))
                result['exact_private_contract_mapping']=private_mapping['source_sha256']
            else:
                result=adapter.evaluate(compiler,case,str(a.validator))
            result['outcome']='passed'
        except Exception as error:result={'name':case['name'],'outcome':'failed','reason':repr(error)}
        records.append(result)
        (a.out/'results.json').write_text(json.dumps({'candidate_library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'old_M10_repairs_preserved':old_changes,'new_mapping':mapping,'cases':records,'failed':[x['name'] for x in records if x['outcome']=='failed']},indent=2)+'\n')
        if result['outcome']=='failed':print(result,flush=True)
    return any(x['outcome']=='failed' for x in records)
if __name__=='__main__':raise SystemExit(main())
