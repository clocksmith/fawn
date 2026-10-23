"""Source-bound ABI1 baseline versus ABI2 storage output equivalence."""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
from api_m5 import Api
import all_fixtures
from evaluate import Evaluator

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['repo','library','validator','evidence']:p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--baseline',type=Path,help='Optional preserved ABI1 shared library; otherwise use source-bound frozen hashes.')
    a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=False)
    old=Evaluator(a.baseline,a.evidence/'unused',a.validator) if a.baseline else None
    frozen=json.loads(Path(__file__).with_name('storage_baseline_sha256.json').read_text()) if old is None else {}
    new=Api(a.library);rows=[]
    for name,source,_ in all_fixtures.admitted(a.repo):
        ns,nr,nw,ne=new.call(source)
        errors=list(ne);source_hash=hashlib.sha256(source).hexdigest()
        output_hash=hashlib.sha256(struct.pack('<'+'I'*len(nw),*nw)).hexdigest()
        if old is not None:
            os,orr,ow,oe=old.call(source);errors.extend(oe)
            if os or ns or ow!=nw:errors.append('retained ABI1 to ABI2 storage binary differs')
        elif ns or frozen[name] != dict(source_sha256=source_hash,output_sha256=output_hash):
            errors.append('retained source-bound ABI1 bytes differ')
        rows.append(dict(case=name,passed=not errors,errors=errors,source_sha256=source_hash,output_sha256=output_hash,word_count=len(nw)))
    result=dict(checks=len(rows),passed=sum(r['passed'] for r in rows),records=rows)
    if old is not None:
        (a.evidence/'storage_baseline_sha256.json').write_text(json.dumps({r['case']:{k:r[k] for k in ['source_sha256','output_sha256']} for r in rows},indent=2)+'\n')
    (a.evidence/'results.json').write_text(json.dumps(result,indent=2)+'\n');new.close();print(json.dumps({k:v for k,v in result.items() if k!='records'}));return int(result['checks']!=result['passed'])
if __name__=='__main__':raise SystemExit(main())
