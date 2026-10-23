"""Focused recheck of four documented M7 evaluator-category corrections."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from api_job_m7 import JobApi
from fixtures_m7 import negatives

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    api=JobApi(args.library);rows=[]
    selected={'unsupported_switch','unsupported_local_const','unsupported_unary_minus','unsuffixed_u32_overflow'}
    for name,source,expected in negatives():
        if name not in selected:continue
        allowed=expected if isinstance(expected,tuple) else (expected,)
        status,result,words,errors=api.call(source)
        if status not in allowed:errors.append(f'status {status} not in {allowed}')
        rows.append(dict(case=name,source_sha256=hashlib.sha256(source).hexdigest(),allowed_statuses=allowed,status=status,diagnostic=result.diagnostic.message.decode(),passed=not errors,errors=errors))
    api.close()
    report=dict(status='PASS' if all(row['passed'] for row in rows) and len(rows)==4 else 'FAIL',records=rows,library_sha256=hashlib.sha256(args.library.read_bytes()).hexdigest(),scope='Four documented evaluator-category corrections only; no full rerun or candidate change.')
    (args.out/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2));return int(report['status']!='PASS')

if __name__=='__main__':raise SystemExit(main())
