"""Only exact amendment2 retained lifts and real fixed token-cap boundaries."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import all_fixtures
from api_m5 import Api
from evaluate import Evaluator
from retained_m7 import execute_lift


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['library','validator','out']:parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    legacy=Evaluator(args.library,args.out/'abi1',args.validator);v2=Api(args.library)
    rows=[]
    selected=[(n,s) for n,s,*_ in all_fixtures.rejected() if n in {'trailing_statement','excess_tokens'}]
    base=selected[0][1].rstrip()[:-1]
    tokens=len(re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]',base))
    for name,source in selected:
        for mode,api in [('ABI1',legacy),('ABI2',v2)]:
            status,result,words,errors=api.call(source)
            if status:errors.append('explicit empty-module lift rejected')
            trials=0
            if not status:
                path=args.out/(name+'_'+mode+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
                val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True)
                if val.returncode:errors.append(val.stderr)
                # ABI1 reflection has a different public shape; the matching v2
                # compile supplies the checked layout, never the semantic oracle.
                rs,rr,rw,result_errors=v2.call(source)
                errors.extend(result_errors)
                if rs or rw!=words:errors.append('current mode words differ')
                try:trials=execute_lift(words,'m4',name,rr.reflection)
                except (ValueError,RuntimeError,AssertionError,KeyError) as exc:errors.append(str(exc))
            rows.append(dict(case=name,mode=mode,status=status,source_sha256=hashlib.sha256(source).hexdigest(),tokens=len(re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]',source)),cpu_trials=trials,passed=not errors,errors=errors))
    for count in [2048,2049]:
        source=base+b';'*(count-tokens)
        for mode,api in [('ABI1',legacy),('ABI2',v2)]:
            status,result,words,errors=api.call(source)
            expected=0 if count==2048 else 1
            if status!=expected:errors.append(f'expected{expected} for{count} tokens, got{status}')
            if count==2049 and b'token' not in result.diagnostic.message.lower():errors.append('missing token budget diagnostic')
            rows.append(dict(case='actual_token_boundary_'+str(count),mode=mode,status=status,tokens=count,source_sha256=hashlib.sha256(source).hexdigest(),passed=not errors,errors=errors))
    v2.close()
    report=dict(status='PASS' if all(r['passed'] for r in rows) else 'FAIL',records=rows,library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),scope='Exactly trailing_statement and excess_tokens amendment2 lifts, plus unchanged actual2048/2049 token boundary; no full rerun.')
    (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));return int(report['status']!='PASS')

if __name__=='__main__':raise SystemExit(main())
