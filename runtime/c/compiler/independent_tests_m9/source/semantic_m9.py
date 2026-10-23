"""Presealed source semantics; actual SPIR-V execution and exact API atomicity."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from api_job_m7 import JobApi, capacities
from api_m5 import Api, reflection_dict
from evaluate import Evaluator
from compute_oracle_m9 import Module, StepLimit, BindingAdmissionError
from semantic_m5 import reflection_bytes
from semantic_m8 import sha,write_json,canonical
from spirv_oracle_base import ExecutionError,StructuralError,OracleLimit
import fixtures_functions_m9
import fixtures_math_m9
import witnesses_m9


def execute_vectors(module: Module,expected: dict) -> dict:
    readonly={(b['group'],b['binding']) for b in expected['bindings'] if b['kind']==1 and b['access']==1}
    invocations=steps=call_count=max_depth=0;opcodes=set()
    for number,vector in enumerate(expected['vectors']):
        actual={k:list(v) for k,v in vector['initial'].items()}
        uniforms={k:bytes(v) for k,v in vector.get('uniforms',{}).items()}
        traces=[]
        for inputs in vector.get('dispatch',[vector['inputs']]):
            traces.extend(module.execute(actual,inputs,readonly,uniforms))
            invocations+=1;steps+=module.last_steps;opcodes.update(module.last_opcodes)
            call_count+=len(module.last_calls)
            max_depth=max([max_depth]+[c[3] for c in module.last_calls])
        differences=[]
        for key,want in vector['want'].items():
            for index,(observed,target) in enumerate(zip(actual[key],want)):
                allowed=vector.get('allowed',{}).get(key,{}).get(index,[target])
                if observed not in allowed:differences.append((key,index,observed,target,allowed))
        if differences:raise ExecutionError(f'vector {number}: wrong raw effects (first8) {differences[:8]}')
        if uniforms!=vector.get('uniforms',{}):raise ExecutionError('uniform bytes changed')
        if 'accesses' in vector and traces!=vector['accesses']:raise ExecutionError('wrong ordered resource trace')
    return dict(cpu_invocations=invocations,instruction_steps=steps,executed_opcodes=sorted(opcodes),user_calls=call_count,maximum_call_depth=max_depth)


def cases(repo: Path,stage: str) -> tuple[list,list]:
    good=[];bad=[]
    if stage in {'functions','all'}:good+=fixtures_functions_m9.positives();bad+=fixtures_functions_m9.negatives()
    if stage in {'math','all'}:good+=fixtures_math_m9.positives();bad+=fixtures_math_m9.negatives()
    if stage in {'witnesses','all'}:good+=witnesses_m9.positives(repo)
    return good,bad


def inventory(repo: Path) -> dict:
    good,bad=cases(repo,'all')
    positive=[dict(case=n,source_sha256=sha(s),bytes=len(s),expected_sha256=sha(json.dumps(canonical(e),sort_keys=True,separators=(',',':')).encode()),vectors=len(e['vectors'])) for n,s,e in good]
    negative=[dict(case=n,source_sha256=sha(s),bytes=len(s),expected_statuses=st) for n,s,st in bad]
    names=[r['case'] for r in positive+negative]
    if len(names)!=len(set(names)):raise ValueError('duplicate exact fixture name')
    return dict(contract_sha256='1b5072f1841a05e8e6feb294ecc5bc7b4a91edc3e7d14d83217fec8ccf34f24a',positive=positive,negative=negative)


def test_positive(api: object,name: str,source: bytes,expected: dict,out: Path,validator: Path) -> tuple[dict,list]:
    status,result,words,errors=api.call(source)
    row=dict(case=name,source_sha256=sha(source),status=status,diagnostic=bytes(result.diagnostic.message).decode(errors='replace'),offset=result.diagnostic.offset)
    if status:errors.append('contract-admitted source rejected')
    else:
        if bytes(result.reflection)!=reflection_bytes(expected):errors.append('reflection differs: '+repr(reflection_dict(result.reflection)))
        raw=struct.pack('<'+'I'*len(words),*words);path=out/(name+'.spv');path.write_bytes(raw)
        row['output_sha256']=sha(raw)
        val=subprocess.run([str(validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
        if val.returncode:errors.append('Vulkan1.1 validation: '+val.stderr)
        try:
            module=Module(words);row['structure']=module.interface(expected)
            row['execution']=execute_vectors(module,expected)
            row['function_count']=len(module.functions)
            row['extinst_operations']=sorted({a[3] for op,a,_ in module.instructions if op==12})
            # No source reimplementation determines behavior. Source inspection
            # here checks only the contract's explicit no-inlining structure.
            declared=source.count(b'fn ')
            if len(module.functions)!=declared:errors.append('not every declared function emitted exactly once')
            if any(op==12 and a[3] in {43,44,45} for op,a,_ in module.instructions):errors.append('unsafe Clamp ExtInst present; contract requires safe composition')
            parameter_ids={a[1] for op,a,_ in module.instructions if op==55}
            if any(op==5 and a[0] in parameter_ids for op,a,_ in module.instructions):errors.append('formal OpName violates4P word ledger')
        except (ExecutionError,StructuralError,OracleLimit,ValueError,AssertionError,KeyError,IndexError,TypeError,ZeroDivisionError) as exc:errors.append(type(exc).__name__+': '+str(exc))
    row.update(errors=errors,passed=not errors)
    return row,words


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','library','validator','out']:parser.add_argument('--'+key,type=Path,required=key in {'repo','out'})
    parser.add_argument('--stage',choices=['functions','math','witnesses','all'],default='all')
    parser.add_argument('--freeze',action='store_true');parser.add_argument('--case',action='append',default=[])
    parser.add_argument('--shared-apis',action='store_true')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    good,bad=cases(args.repo,args.stage)
    if args.freeze:
        data=inventory(args.repo);write_json(args.out/'inventory.json',data)
        write_json(args.out/'expectations.json',{n:e for n,s,e in good})
        # Small exact sources are retained; large deterministic generators remain
        # source-sealed and are verified by the exact inventory hashes.
        directory=args.out/'sources';directory.mkdir()
        for n,s,*rest in good+bad:
            if len(s)<8192:(directory/(n+'.wgsl')).write_bytes(s)
        print(json.dumps(dict(positive=len(good),negative=len(bad),inventory_sha256=sha((args.out/'inventory.json').read_bytes()))));return 0
    if args.library is None or args.validator is None:parser.error('execution requires explicit library and validator')
    if args.case:
        good=[c for c in good if c[0] in args.case];bad=[c for c in bad if c[0] in args.case]
        if len(good)+len(bad)!=len(set(args.case)):raise ValueError('unknown or duplicate case')
    api=JobApi(args.library,capacities(source_bytes=524288,tokens=65536,expressions=16384,statements=4096,words=524288,expression_depth=64,control_depth=32))
    fixed=Api(args.library) if args.shared_apis else None
    abi1=Evaluator(args.library,args.out/'abi1',args.validator) if args.shared_apis else None
    directory=args.out/'modules';directory.mkdir();rows=[]
    for name,source,expected in good:
        row,words=test_positive(api,name,source,expected,directory,args.validator)
        if fixed is not None and not expected.get('job_only') and len(source)<800:
            for label,adapter in [('v2',fixed),('abi1',abi1)]:
                status,result,other,errors=adapter.call(source)
                if label=='abi1' and expected['structs']:
                    if not status:row['errors'].append('ABI1 published unrepresentable struct reflection')
                    continue
                if status:errors.append('small shared source rejected: '+bytes(result.diagnostic.message).decode(errors='replace'))
                elif words!=other:errors.append('current words differ between public adapters')
                row['errors'] += [label+': '+e for e in errors]
        row['passed']=not row['errors'];rows.append(row)
        if not row['passed']:print(json.dumps(row),flush=True)
    for name,source,statuses in bad:
        status,result,words,errors=api.call(source)
        if status not in statuses:errors.append(f'expected rejection {statuses}, got {status}')
        row=dict(case=name,status=status,source_sha256=sha(source),diagnostic=bytes(result.diagnostic.message).decode(errors='replace'),offset=result.diagnostic.offset,errors=errors,passed=not errors)
        rows.append(row)
        if errors:print(json.dumps(row),flush=True)
    report=dict(stage=args.stage,positive=len(good),negative=len(bad),passed=sum(r['passed'] for r in rows),failed=sum(not r['passed'] for r in rows),cpu_invocations=sum(r.get('execution',{}).get('cpu_invocations',0) for r in rows),records=rows,physical_execution=False,runtime_adoption=False)
    write_json(args.out/'results.json',report);print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))
    api.close()
    if fixed is not None:fixed.close()
    return int(bool(report['failed']))


if __name__=='__main__':raise SystemExit(main())
