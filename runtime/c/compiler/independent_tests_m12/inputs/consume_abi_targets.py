"""Validate and execute native-harness emitted targets using frozen case oracles."""
from __future__ import annotations
import argparse
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from exact_numeric_m8 import finite_value
from run_cases import verify
from target_m12 import Module
from workgroup_m12 import dispatch

ROOT=Path(__file__).resolve().parent


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();verify();a.out.mkdir(exist_ok=False)
    cases={r['id']:r for r in json.loads((ROOT/'cases.json').read_text())['cases']};rows=[]
    for item in json.loads(a.manifest.read_text())['targets']:
        case=cases[item['case_id']];path=Path(item['path']);raw=path.read_bytes();row=dict(item,target_sha256=hashlib.sha256(raw).hexdigest())
        try:
            if 'sha256' in item:assert row['target_sha256']==item['sha256']
            check=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True);assert check.returncode==0,check.stderr
            words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));module=Module(words)
            buffers={(0,1):bytearray(b'\xa5'*(case['output_words']*4))}
            if 'input_words' in case:buffers[(0,0)]=bytearray(struct.pack('<'+'I'*len(case['input_words']),*case['input_words']))
            original={k:bytes(v) for k,v in buffers.items()}
            result=dispatch(module,buffers,(1,1,1),'round_robin',pure_quantum=1)
            actual=list(struct.unpack('<'+'I'*case['output_words'],buffers[(0,1)]))
            expected=case.get('expected_options',case.get('runtime_intervals',[]));n=len(expected)
            assert actual[n:]==[0xa5a5a5a5]*(len(actual)-n),'output guard'
            for index,(got,wanted) in enumerate(zip(actual,expected)):
                if 'expected_options' in case:assert got in wanted,(index,got,wanted)
                else:
                    low,high=map(Fraction,wanted);assert low<=finite_value(got)<=high,(index,got,wanted)
            if 'expected_input_words' in case:
                desired=struct.pack('<'+'I'*len(case['expected_input_words']),*case['expected_input_words']);assert buffers[(0,0)]==desired,'source effects'
            elif (0,0) in buffers:assert buffers[(0,0)]==original[(0,0)],'unexpected input mutation'
            row.update(passed=True,execution=result,actual_words=actual)
        except Exception as error:row.update(passed=False,error=repr(error))
        rows.append(row)
    report={'input_manifest_sha256':hashlib.sha256(a.manifest.read_bytes()).hexdigest(),'targets':rows,'passed':all(x['passed'] for x in rows)}
    (a.out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n');print(json.dumps({'targets':len(rows),'passed':report['passed']}));return int(not report['passed'])

if __name__=='__main__':raise SystemExit(main())
