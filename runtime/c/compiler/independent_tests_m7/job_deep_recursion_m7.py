"""Focused combined nesting/continuing and wide-sibling public consumer."""
from __future__ import annotations
import argparse
import json
import struct
from pathlib import Path
from api_job_m7 import JobApi, capacities, INPUT_ERROR, diagnostic


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    for controls,expression_depth,expected in [(32,64,0),(32,65,INPUT_ERROR),(33,64,INPUT_ERROR)]:
        body='let x='+'('*(expression_depth-1)+'1u'+')'*(expression_depth-1)+';'
        for _ in range(controls):
            body='loop {continuing {'+body+'break if true;}}'
        source=('@compute @workgroup_size(1) fn main(){'+body+'}').encode()
        api=JobApi(args.library,capacities(control_depth=32,expression_depth=64))
        status,result,words,errors=api.call(source)
        errors=list(errors)
        if status!=expected:
            errors.append(f'expected {expected}, got {status}')
        if not status:
            (args.out/f'controls{controls}-expressions{expression_depth}.spv').write_bytes(struct.pack('<'+'I'*len(words),*words))
        rows.append(dict(controls=controls,expression_depth=expression_depth,status=status,expected=expected,errors=errors,diagnostic=diagnostic(result),word_count=len(words)))
        api.close()
    source=b'@compute @workgroup_size(1) fn main(){'+b';'*4096+b'}'
    api=JobApi(args.library,capacities(statements=4096,expressions=0,control_depth=1,expression_depth=1))
    status,result,words,errors=api.call(source)
    errors=list(errors)
    if status:
        errors.append('wide sibling list rejected')
    if not status:
        (args.out/'siblings4096.spv').write_bytes(struct.pack('<'+'I'*len(words),*words))
    rows.append(dict(sibling_statements=4096,status=status,errors=errors,word_count=len(words)))
    api.close()
    report=dict(status='FAIL' if any(row['errors'] for row in rows) else 'PASS',records=rows,
                provenance='Post-freeze supplemental path realization for already frozen combined-depth and no-sibling-recursion obligations; source-derived structural shape, no expected output learned from candidate.')
    (args.out/'combined-recursion-cases.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    return int(report['status']!='PASS')

if __name__=='__main__':
    raise SystemExit(main())
