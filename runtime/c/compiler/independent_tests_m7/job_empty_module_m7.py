"""Pre-candidate supplemental M7 empty module declaration acceptance.

Frozen independently before candidate inspection for the narrow reviewed
job-only grammar amendment. No statement/node budget exemption is inferred
for semicolons inside a function or inside any declaration.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

from api_m5 import Api
from api_job_m7 import JobApi, capacities, EMPTY, STORE, INPUT_ERROR, PARSE_ERROR, diagnostic
from evaluate import Evaluator


POSITIVES = [
    ('leading', b';'+EMPTY),
    ('trailing', EMPTY+b';'),
    ('repeated', b';; ;\n;'+EMPTY+b';;\n;'),
    ('after_resource', STORE.replace(b'; @compute', b';; ; @compute')),
    ('between_resources', b'@group(0) @binding(0) var<storage,read> src:array<u32>;;\n;'
     b'@group(0) @binding(1) var<storage,read_write> dst:array<u32>;;'
     b'@compute @workgroup_size(1) fn main(){dst[0u]=src[0u];};;'),
    ('after_constant', b'const COUNT:u32=1u;;;'+EMPTY+b';'),
    ('after_struct', b'struct Params { x:u32, };;'+EMPTY+b';'),
    ('between_struct_resource', b';struct Params { x:u32, };;\n'
     b'@group(0) @binding(0) var<uniform> p:Params;;\n'
     b'@group(0) @binding(1) var<storage,read_write> out:array<u32>;;\n'
     b'@compute @workgroup_size(1) fn main(){out[0u]=p.x;};;'),
]
NEGATIVES = [
    ('attribute_name', b'@;compute @workgroup_size(1) fn main(){}'),
    ('attribute_argument', b'@compute @workgroup_size(1;) fn main(){}'),
    ('between_attribute_and_function', b'@compute; @workgroup_size(1) fn main(){}'),
    ('array_type', STORE.replace(b'array<u32>', b'array<;u32>')),
    ('resource_declaration', STORE.replace(b'out:array', b';out:array')),
    ('resource_address_space', STORE.replace(b'storage,read_write', b'storage,;read_write')),
    ('struct_member_name', b'struct Params{x;:u32,}'+EMPTY),
    ('struct_member_type', b'struct Params{x:;u32,}'+EMPTY),
    ('function_name', EMPTY.replace(b'fn main', b'fn ;main')),
    ('constant_declaration', b'const COUNT;:u32=1u;'+EMPTY),
]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    first_failure=None
    job=JobApi(args.library)
    old=[('ABI1',Evaluator(args.library,args.out/'abi1',Path('/not-used'))),('ABI2',Api(args.library))]
    def record(name: str, api: str, source: bytes, expected: int | None, consumer: object) -> None:
        nonlocal first_failure
        status,result,words,errors=consumer.call(source)
        errors=list(errors)
        if expected is not None and status!=expected:
            errors.append(f'status {status}, expected {expected}')
        if expected is None and status==0:
            errors.append('required rejection instead admitted this source')
        row=dict(name=name,api=api,status=status,expected=expected,word_count=len(words),diagnostic=diagnostic(result),errors=errors)
        rows.append(row)
        if errors and first_failure is None:
            first_failure=row
    for name,source in POSITIVES:
        record(name,'job',source,0,job)
        for label,consumer in old:
            record(name,label,source,None,consumer)
    for name,source in NEGATIVES:
        record(name,'job',source,PARSE_ERROR,job)
    for name,source in [('semicolon_only',b';; ;\n;'),('struct_no_entry',b';struct P{x:u32,};;')]:
        record(name,'job',source,None,job)
    source=b';;\n'+EMPTY+b';;;'
    token_count=len(re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]',source))
    tests=[('no_source_statement_node_charge',dict(statements=0,expressions=0),0),
           ('exact_token_charge',dict(tokens=token_count,statements=0,expressions=0),0),
           ('one_token_short',dict(tokens=token_count-1,statements=0,expressions=0),INPUT_ERROR),
           ('source_bytes_still_charge',dict(source_bytes=len(source)-1,statements=0,expressions=0),INPUT_ERROR)]
    for name,changes,expected in tests:
        api=JobApi(args.library,capacities(**changes),extra_workspace=8192)
        record(name,'job',source,expected,api)
        api.close()
    body_source=b';;'+EMPTY[:-1]+b';};;'
    api=JobApi(args.library,capacities(statements=0,expressions=0))
    record('function_semicolon_still_charges_statement','job',body_source,INPUT_ERROR,api)
    api.close()
    job.close()
    old[1][1].close()
    report=dict(status='FAIL' if first_failure else 'PASS',checks=len(rows),first_failure=first_failure,records=rows,
                scope='Job-only generic empty module declarations; legacy rejection and interior parse errors; source/token costs retained.')
    (args.out/'empty-module.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=report['status'],checks=len(rows),first_failure=first_failure),indent=2))
    return int(first_failure is not None)


if __name__=='__main__':
    raise SystemExit(main())
