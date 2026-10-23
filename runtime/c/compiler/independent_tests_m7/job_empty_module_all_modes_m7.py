"""Independent amendment2 empty-module admission, paired with unchanged sources.

The initial amendment1 supplement remains intact. This file alone supersedes
its job-only expectation; no unrelated legacy capability is widened.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import subprocess

from api_m5 import Api
from api_job_m7 import JobApi, capacities, EMPTY, STORE, INPUT_ERROR, PARSE_ERROR, UNSUPPORTED, diagnostic
from evaluate import Evaluator
from job_empty_module_m7 import NEGATIVES


PAIRS = [
    ('leading',b';'+EMPTY,EMPTY,False),
    ('trailing',EMPTY+b';',EMPTY,False),
    ('repeated',b';; ;\n;'+EMPTY+b';;\n;',EMPTY,False),
    ('after_resource',STORE.replace(b'; @compute',b';; ; @compute'),STORE,False),
    ('between_resources',
     b'@group(0) @binding(0) var<storage,read> src:array<u32>;;\n;@group(0) @binding(1) var<storage,read_write> dst:array<u32>;;@compute @workgroup_size(1) fn main(){dst[0u]=src[0u];};;',
     b'@group(0) @binding(0) var<storage,read> src:array<u32>;\n@group(0) @binding(1) var<storage,read_write> dst:array<u32>;@compute @workgroup_size(1) fn main(){dst[0u]=src[0u];}',False),
    ('after_constant',b'const COUNT:u32=1u;;;'+EMPTY+b';',b'const COUNT:u32=1u;'+EMPTY,False),
    ('after_struct',b'struct Params{x:u32,};;'+EMPTY+b';',b'struct Params{x:u32,}'+EMPTY,True),
    ('between_struct_resource',
     b';struct Params{x:u32,};;@group(0) @binding(0) var<uniform> p:Params;;@group(0) @binding(1) var<storage,read_write> out:array<u32>;;@compute @workgroup_size(1) fn main(){out[0u]=p.x;};;',
     b'struct Params{x:u32,}@group(0) @binding(0) var<uniform> p:Params;@group(0) @binding(1) var<storage,read_write> out:array<u32>;@compute @workgroup_size(1) fn main(){out[0u]=p.x;}',True),
]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--cli',type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    job=JobApi(args.library)
    apis=[('ABI1',Evaluator(args.library,args.out/'abi1',Path('/not-used'))),('ABI2',Api(args.library)),('job',job)]
    for name,source,baseline,structs in PAIRS:
        for label,api in apis:
            expected=UNSUPPORTED if label=='ABI1' and structs else 0
            bs,br,bw,be=api.call(baseline)
            status,result,words,errors=api.call(source)
            errors=list(errors)+list(be)
            if bs!=expected or status!=expected:
                errors.append(f'paired status baseline={bs}, semicolons={status}, expected={expected}')
            if expected==0 and words!=bw:
                errors.append('empty module declarations changed current emitted words')
            if expected==0 and bytes(result.reflection)!=bytes(br.reflection):
                errors.append('empty module declarations changed reflection')
            rows.append(dict(name=name,api=label,expected=expected,status=status,baseline_status=bs,errors=errors,diagnostic=diagnostic(result)))
        if args.cli:
            for mode,flag in [('binary',None),('v2','--reflection-v2'),('legacy','--reflection')]:
                emitted=[]
                errors=[]
                for variant,data in [('baseline',baseline),('empty_modules',source)]:
                    directory=args.out/(name+'_'+mode+'_'+variant)
                    directory.mkdir()
                    src=directory/'source.wgsl'
                    src.write_bytes(data)
                    command=[str(args.cli.resolve()),'--shader-path',str(src),'--out',str(directory/'module.spv')]
                    if flag:
                        command += [flag,str(directory/'reflection.json')]
                    run=subprocess.run(command,capture_output=True,text=True,timeout=30,check=False)
                    expected_failure=mode=='legacy' and structs
                    if expected_failure:
                        if run.returncode==0 or {p.name for p in directory.iterdir()}!={'source.wgsl'}:
                            errors.append('legacy struct capability or failure publication changed')
                    elif run.returncode:
                        errors.append(variant+' rejected: '+run.stderr)
                    else:
                        emitted.append((directory/'module.spv').read_bytes())
                if len(emitted)==2 and emitted[0]!=emitted[1]:
                    errors.append('paired CLI empty declarations changed words')
                rows.append(dict(name=name,api='cli_'+mode,errors=errors))
    for name,source in NEGATIVES:
        status,result,words,errors=job.call(source)
        errors=list(errors)
        if status!=PARSE_ERROR:
            errors.append(f'malformed declaration status {status}, expected PARSE_ERROR')
        rows.append(dict(name=name,api='job',status=status,errors=errors,diagnostic=diagnostic(result)))
    for name,source in [('semicolon_only',b';; ;\n;'),('struct_no_entry',b';struct P{x:u32,};;')]:
        status,result,words,errors=job.call(source)
        errors=list(errors)
        if status==0:
            errors.append('missing-entry module admitted')
        rows.append(dict(name=name,api='job',status=status,errors=errors,diagnostic=diagnostic(result)))
    source=b';;\n'+EMPTY+b';;;'
    tokens=len(re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]',source))
    tests=[('no_statement_node_charge',source,dict(statements=0,expressions=0),0),
           ('exact_token_charge',source,dict(tokens=tokens,statements=0,expressions=0),0),
           ('one_token_short',source,dict(tokens=tokens-1,statements=0,expressions=0),INPUT_ERROR),
           ('source_bytes_charge',source,dict(source_bytes=len(source)-1,statements=0,expressions=0),INPUT_ERROR),
           ('function_semicolon_charges_statement',b';;'+EMPTY[:-1]+b';};;',dict(statements=0,expressions=0),INPUT_ERROR)]
    for name,source,changes,expected in tests:
        api=JobApi(args.library,capacities(**changes),extra_workspace=8192)
        status,result,words,errors=api.call(source)
        errors=list(errors)
        if status!=expected:
            errors.append(f'budget status {status}, expected {expected}')
        rows.append(dict(name=name,api='job',status=status,errors=errors,diagnostic=diagnostic(result)))
        api.close()
    job.close()
    apis[1][1].close()
    failures=[row for row in rows if row['errors']]
    report=dict(status='FAIL' if failures else 'PASS',checks=len(rows),failed=failures,records=rows,
                amendment='c13b358f9cd2841efce42ec0647acfe8849b33cd',
                scope='Only generic empty module syntax is lifted across APIs/modes; ABI1 struct/uniform rejection remains.')
    (args.out/'empty-module-all-modes.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=report['status'],checks=len(rows),first_failure=failures[0] if failures else None),indent=2))
    return int(bool(failures))


if __name__=='__main__':
    raise SystemExit(main())
