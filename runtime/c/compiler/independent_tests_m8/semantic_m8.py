"""Frozen-source scalar semantics, original-source diagnostics and API parity."""
from __future__ import annotations

import argparse
import ast
import re
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import ctypes as C

from api_job_m7 import JobApi, capacities
from api_m5 import Api, reflection_dict
from evaluate import Evaluator
from compute_oracle_m8 import Module, StepLimit, BindingAdmissionError
from semantic_m5 import reflection_bytes
from spirv_oracle_base import ExecutionError, StructuralError, OracleLimit
import fixtures_scalar_m8
import fixtures_constants_m8
import witnesses_m8


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> object:
    if isinstance(value,bytes):return {'bytes_hex':value.hex()}
    if isinstance(value,dict):return {str(k):canonical(v) for k,v in sorted(value.items(),key=lambda x:str(x[0]))}
    if isinstance(value,(tuple,list)):return [canonical(x) for x in value]
    return value


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(canonical(value),indent=2,sort_keys=True)+'\n',encoding='utf-8')


def execute_vectors(module: Module, expected: dict) -> dict:
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['kind']==1 and b['access']==1}
    count=steps=unfinished=0;opcodes=set();dispatches=0
    for number,vector in enumerate(expected['vectors']):
        actual={k:list(v) for k,v in vector['initial'].items()}
        uniforms={k:bytes(v) for k,v in vector.get('uniforms',{}).items()}
        inputs=vector.get('dispatch',[vector['inputs']]);dispatches+=int('dispatch' in vector)
        traces=[]
        for builtin_values in inputs:
            try:
                trace=module.execute(actual,builtin_values,readonly,uniforms,
                    step_limit=10000 if expected.get('unfinished') else 1000000)
                traces.extend(trace)
            except StepLimit:
                if not expected.get('unfinished'):raise
                unfinished+=1;continue
            if expected.get('unfinished'):raise ExecutionError('dynamically infinite fixture terminated')
            count+=1;steps+=module.last_steps;opcodes.update(module.last_opcodes)
        if expected.get('unfinished'):continue
        if actual!=vector['want']:
            differences=[(k,i,a,b) for k in actual for i,(a,b) in enumerate(zip(actual[k],vector['want'][k])) if a!=b]
            raise ExecutionError(f'vector {number}: wrong raw effects (first8) {differences[:8]}')
        if uniforms!=vector.get('uniforms',{}):raise ExecutionError('uniform bytes changed')
        if 'accesses' in vector and traces!=vector['accesses']:
            raise ExecutionError(f'vector {number}: wrong access order {traces} != {vector["accesses"]}')
    return dict(cpu_invocations=count,unfinished=unfinished,instruction_steps=steps,
                executed_opcodes=sorted(opcodes),dispatch_vectors=dispatches)


def source_cases(repo: Path, selected: str) -> tuple[list,list]:
    positive=[];negative=[]
    if selected in {'scalar','all'}:
        positive+=fixtures_scalar_m8.positives();negative+=fixtures_scalar_m8.negatives()
    if selected in {'constants','all'}:
        positive+=fixtures_constants_m8.positives();negative+=fixtures_constants_m8.negatives()
    if selected in {'witnesses','all'}:positive+=witnesses_m8.positives(repo)
    if selected in {'scalar','all'}:
        header=(Path(__file__).parent/'native_host/fixtures_m8.h').read_text(encoding='utf-8')
        section=header.split('static const char numeric_source[] =',1)[1].split('/* Mathematical',1)[0]
        source=''.join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"',section)).encode()
        from fixtures_m5 import storage
        from fixtures_m6 import trial
        values=[0x3f800000,0x3f800002,1,0x80000000,0x25800000,0,0x00400000,0x3dcccccd]
        expected=dict(entry='main',n=[1,1,1],bindings=[storage('out',0)],structs=[],builtins=[],
                      vectors=[trial({(0,0):[0xa55a36c9]*10},{(0,0):dict(enumerate(values))})])
        positive.append(('native_host_numeric_fixture',source,expected))
    return positive,negative


def inventory(repo: Path) -> dict:
    positive,negative=source_cases(repo,'all')
    p=[dict(name=n,source_sha256=sha(s),source_bytes=len(s),
            expected_sha256=sha(json.dumps(canonical(e),sort_keys=True,separators=(',',':')).encode()),
            vector_count=len(e['vectors'])) for n,s,e in positive]
    n=[dict(name=name,source_sha256=sha(s),source_bytes=len(s),expected_statuses=st) for name,s,st in negative]
    names=[x['name'] for x in p+n]
    if len(names)!=len(set(names)):raise ValueError('duplicate independent case name')
    return dict(contract='0e1e201030c60474e07db99cdbb1a5ea025d3989',positive=p,negative=n)


def test_positive(api: object, name: str, source: bytes, expected: dict,
                  out: Path, validator: Path) -> tuple[dict,list[int]]:
    status,result,words,errors=api.call(source)
    row=dict(case=name,source_sha256=sha(source),status=status,
             diagnostic=bytes(result.diagnostic.message).decode('utf-8','replace'),
             offset=result.diagnostic.offset)
    if status:errors.append('contract-admitted source rejected')
    else:
        if bytes(result.reflection)!=reflection_bytes(expected):
            errors.append('reflection differs, including unused bytes: '+repr(reflection_dict(result.reflection)))
        raw=struct.pack('<'+'I'*len(words),*words);path=out/(name+'.spv');path.write_bytes(raw)
        row['output_sha256']=sha(raw)
        validator_result=subprocess.run([str(validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,timeout=30,check=False)
        if validator_result.returncode:errors.append('Vulkan1.1 validation: '+validator_result.stderr)
        try:
            module=Module(words);row['structure']=module.interface(expected)
            row['execution']=execute_vectors(module,expected)
            # Uniform minimum binding admission is not storage robustness.
            if expected['vectors']:
                vector=expected['vectors'][0]
                for key,minimum in module.uniform_minimums.items():
                    for length in [0,minimum-1]:
                        actual={k:list(v) for k,v in vector['initial'].items()}
                        short=dict(vector['uniforms']);short[key]=short[key][:length]
                        try:module.execute(actual,vector['inputs'],set(),short)
                        except BindingAdmissionError:pass
                        else:raise ExecutionError('short uniform admitted')
                        if actual!=vector['initial']:raise ExecutionError('short uniform touched storage')
        except (ExecutionError,StructuralError,OracleLimit,ValueError,AssertionError,KeyError,IndexError,TypeError,ZeroDivisionError) as exc:
            errors.append(type(exc).__name__+': '+str(exc))
    row.update(errors=errors,passed=not errors)
    return row,words


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path,required=True,help='Pinned checkout for historical imports and witnesses.')
    parser.add_argument('--library',type=Path,help='Explicit candidate source-built library.')
    parser.add_argument('--validator',type=Path,help='Installed spirv-val.')
    parser.add_argument('--out',type=Path,required=True,help='Fresh output directory.')
    parser.add_argument('--stage',choices=['scalar','constants','witnesses','all'],default='all',help='Focused source family.')
    parser.add_argument('--case',action='append',default=[],help='Run only an exact named case, preserving all original expectations.')
    parser.add_argument('--freeze',action='store_true',help='Seal sources/expectations only without reading candidate.')
    parser.add_argument('--apply-errata',action='store_true',help='Apply separately documented narrow evaluator corrections.')
    parser.add_argument('--shared-apis',action='store_true',help='Compare small successful sources across ABI1/v2/job.')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    if args.freeze:
        data=inventory(args.repo);write_json(args.out/'inventory.json',data)
        positive,negative=source_cases(args.repo,'all')
        sources=args.out/'sources';sources.mkdir()
        for name,source,*_ in positive+negative:(sources/(name+'.wgsl')).write_bytes(source)
        write_json(args.out/'expected.json',{n:e for n,s,e in positive})
        print(json.dumps(dict(positive=len(positive),negative=len(negative),inventory_sha256=sha((args.out/'inventory.json').read_bytes()))));return 0
    if args.library is None or args.validator is None:parser.error('candidate run needs --library and --validator')
    positive,negative=source_cases(args.repo,args.stage)
    if args.apply_errata:
        from evaluator_errata_m8 import apply
        positive,negative=apply(positive,negative)
    if args.case:
        positive=[c for c in positive if c[0] in args.case];negative=[c for c in negative if c[0] in args.case]
        if len(positive)+len(negative)!=len(set(args.case)):raise ValueError('case filter contains unknown/duplicate cases')
    modules=args.out/'modules';modules.mkdir()
    api=JobApi(args.library,capacities(tokens=16384,expressions=8192,statements=2048,words=131072,expression_depth=64,control_depth=32))
    fixed=Api(args.library) if args.shared_apis else None
    abi1=Evaluator(args.library,args.out/'abi1',args.validator) if args.shared_apis else None
    rows=[]
    for name,source,expected in positive:
        row,words=test_positive(api,name,source,expected,modules,args.validator)
        if fixed is not None and not expected.get('job_only') and len(source)<800:
            for label,adapter in [('v2',fixed),('abi1',abi1)]:
                status,result,other,errors=adapter.call(source)
                if label=='abi1' and expected['structs']:
                    if not status:row['errors'].append('ABI1 published unrepresentable struct reflection')
                    continue
                # Long function fixture is a real S limit, not syntax exclusion.
                if name=='signed_compound_matrix':
                    if status!=1:errors.append('expected real fixed statement limit INPUT_ERROR')
                else:
                    if status:errors.append('small shared syntax rejected: '+bytes(result.diagnostic.message).decode('utf-8','replace'))
                    elif words!=other:errors.append('current emitted words differ across APIs')
                row['errors'] += [label+': '+e for e in errors]
            row['passed']=not row['errors']
        rows.append(row)
        if not row['passed']:print(json.dumps(row),flush=True)
    for name,source,statuses in negative:
        status,result,_,errors=api.call(source)
        if status not in statuses:errors.append(f'expected rejection statuses {statuses}, observed {status}')
        row=dict(case=name,source_sha256=sha(source),status=status,expected_statuses=statuses,
                 diagnostic=bytes(result.diagnostic.message).decode('utf-8','replace'),offset=result.diagnostic.offset,
                 errors=errors,passed=not errors)
        rows.append(row)
        if errors:print(json.dumps(row),flush=True)
    report=dict(kind='Independent M8 source-built semantic stage',stage=args.stage,
                library_sha256=sha(args.library.read_bytes()),validator_sha256=sha(args.validator.read_bytes()),
                positive=len(positive),negative=len(negative),passed=sum(r['passed'] for r in rows),
                failed=sum(not r['passed'] for r in rows),cpu_invocations=sum(r.get('execution',{}).get('cpu_invocations',0) for r in rows),
                records=rows,physical_gpu_execution='not_run',runtime_adoption='not_run')
    write_json(args.out/'results.json',report)
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))
    api.close()
    if fixed is not None:fixed.close()
    return int(report['failed']!=0)


if __name__=='__main__':raise SystemExit(main())
