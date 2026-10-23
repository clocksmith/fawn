"""Independent exact M8 constant/attribute/legacy accounting and capacity checks."""
from __future__ import annotations

import argparse
import ctypes as C
import json
from pathlib import Path

from api_job_m7 import JobApi, capacities, cap_dict, copy_caps, Capacities
from api_m5 import Api, Requirements
from evaluate import Evaluator
from semantic_m8 import write_json, sha, test_positive
from fixtures_m5 import storage
from fixtures_m6 import program, trial

EMPTY='@compute @workgroup_size(1) fn main(){}'


def exact_cases() -> list:
    rows=[]
    def add(name: str,source: str,e: int,s: int,accepted: bool,budget: str='expression') -> None:
        rows.append(dict(name=name,source=source,expressions=e,statements=s,accepted=accepted,budget=budget))
    add('zero_empty_bare_attributes',EMPTY,0,0,True)
    add('legacy_leading_const_exempt','const K:u32=1u;'+EMPTY,0,0,True)
    add('legacy_exempt_plus_one','const K:u32=1u;const J=2;'+EMPTY,1,0,True)
    add('legacy_exempt_plus_two_exhausts','const K:u32=1u;const J=2;const H=3;'+EMPTY,1,0,False)
    add('new_leading_const_charged','const K=1;'+EMPTY,1,0,True)
    add('new_leading_const_zero_budget','const K=1;'+EMPTY,0,0,False)
    add('typed_complex_const_three_nodes','const K:u32=1u+2u;'+EMPTY,3,0,True)
    add('typed_complex_const_two_nodes','const K:u32=1u+2u;'+EMPTY,2,0,False)
    add('trailing_const_charged',EMPTY+'const K:u32=1u;',1,0,True)
    add('trailing_const_not_exempt',EMPTY+'const K:u32=1u;',0,0,False)
    attr='const K:u32=1u;@compute @workgroup_size(K) fn main(){}'
    add('attribute_reference_one_node',attr,1,0,True)
    add('attribute_reference_exhausted',attr,0,0,False)
    attr='const K=1u;@compute @workgroup_size(K) fn main(){}'
    add('const_attribute_total_two',attr,2,0,True)
    add('const_attribute_total_one',attr,1,0,False)
    attr='@compute @workgroup_size(1+1) fn main(){}'
    add('attribute_expression_three',attr,3,0,True)
    add('attribute_expression_two',attr,2,0,False)
    local='@compute @workgroup_size(1) fn main(){const K=1;}'
    add('local_const_statement_node',local,1,1,True)
    add('local_const_no_statement',local,1,0,False,'statement')
    add('local_const_no_node',local,0,1,False)
    return rows


def ir_source() -> tuple:
    condition=' && '.join(f'src[{i}u]>0u' for i in range(4))
    source,expected=program(''.join(f'out[{i}u]=u32({condition});' for i in range(15)),
                            bindings=[storage('out',0),storage('src',1,access=1)],parameters=[])
    expected['vectors']=[trial({(0,0):[99]*17,(0,1):a},{(0,0):{i:int(all(a)) for i in range(15)}})
                         for a in [[1,2,3,4],[1,0,3,4]]]
    return source,expected


def true_ir_source() -> tuple:
    from fixtures_scalar_m8 import make
    def tree(n: int) -> str:
        return 'flag' if n==1 else '('+tree(n//2)+' && '+tree(n-n//2)+')'
    _,source,expected=make('balanced_mutable_fixed_ir','var flag=src[0u]>0u;'+''.join(f'out[{i}u]=u32({tree(10)});' for i in range(15)),
        vectors=[trial({(0,0):[99]*17,(0,1):[v]},{(0,0):{i:int(bool(v)) for i in range(15)}}) for v in [0,1]])
    return source,expected


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',type=Path,help='Explicit source-built candidate.')
    parser.add_argument('--validator',type=Path,help='Installed validator.')
    parser.add_argument('--out',type=Path,required=True,help='Fresh evidence directory.')
    parser.add_argument('--apply-errata',action='store_true',help='Narrow separately documented capacity corrections and additional true IR stress.')
    parser.add_argument('--freeze',action='store_true',help='Only record source/capacity expectations.')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    cases=exact_cases();source,expected=ir_source()
    if args.freeze:
        write_json(args.out/'inventory.json',dict(exact_cases=cases,ir_source=source,
            ir_source_sha256=sha(source),ir_expected=expected,
            independent_accounting=dict(statements=15,nodes=315,source_depth_below=18),
            layout_bound='C=E+1;I=1+4E+8S;IDs=256+32I; all products and final header checked'))
        print(sha((args.out/'inventory.json').read_bytes()));return 0
    if args.library is None or args.validator is None:parser.error('run needs library and validator')
    rows=[]
    for case in cases:
        caps=capacities(expressions=case['expressions'],statements=case['statements'])
        api=JobApi(args.library,caps);status,result,_,errors=api.call(case['source'].encode())
        if status!=(0 if case['accepted'] else 1):errors.append('exact node/statement accounting differs')
        message=bytes(result.diagnostic.message).decode('utf-8','replace')
        if not case['accepted'] and case['budget'] not in message.lower():
            if not (args.apply_errata and case['name']=='legacy_exempt_plus_two_exhausts' and 'constant capacity' in message.lower()):errors.append('exhaustion lacks named budget')
        rows.append(dict(case=case['name'],status=status,diagnostic=message,errors=errors,passed=not errors));api.close()
    # Three consecutive exact workspace reuses must never treat spare physical bytes as configured E.
    api=JobApi(args.library,capacities(expressions=0,statements=0),extra_workspace=4096)
    for n in range(3):
        status,result,_,errors=api.call(('const K=1;'+EMPTY).encode())
        if status!=1:errors.append('extra workspace expanded configured node capacity')
        rows.append(dict(case='spare_physical_not_capacity_'+str(n),status=status,errors=errors,passed=not errors))
    api.close()
    good=JobApi(args.library,capacities(expressions=512,statements=16,words=65536))
    modules=args.out/'modules';modules.mkdir()
    row,_=test_positive(good,'new_fixed_ir_stress_job',source,expected,modules,args.validator);rows.append(row);good.close()
    for label,adapter in [('abi1',Evaluator(args.library,args.out/'abi1',args.validator)),('v2',Api(args.library))]:
        status,result,_,errors=adapter.call(source)
        message=bytes(result.diagnostic.message).decode('utf-8','replace')
        if args.apply_errata:
            if status!=5 or 'word budget' not in message.lower():errors.append('original pressure case must reject at independent fixed word budget')
        elif status!=1 or not any(word in message.lower() for word in ['instruction','ir']):
            errors.append('315-node/15-statement stress must expose real fixed IR exhaustion before words')
        rows.append(dict(case='fixed_ir_stress_'+label,status=status,diagnostic=message,errors=errors,passed=not errors))
        if label=='v2':adapter.close()
    if args.apply_errata:
        source,expected=true_ir_source()
        good=JobApi(args.library,capacities(expressions=512,statements=16,words=65536))
        row,_=test_positive(good,'postinspection_true_ir_stress',source,expected,modules,args.validator);rows.append(row);good.close()
        for label,adapter in [('abi1',Evaluator(args.library,args.out/'ir-abi1',args.validator)),('v2',Api(args.library))]:
            status,result,_,errors=adapter.call(source);message=bytes(result.diagnostic.message).decode()
            if status!=1 or 'instruction capacity' not in message:errors.append('balanced mutable stress must expose typed IR capacity')
            rows.append(dict(case='postinspection_true_ir_'+label,status=status,diagnostic=message,errors=errors,passed=not errors))
            if label=='v2':adapter.close()
    # ID arithmetic must include the declared 32-ID expansion and final bound.
    query=JobApi(args.library);max_e=(((0xffffffff-256)//32)-1)//4
    for name,caps in [('target_id_e_overflow',capacities(expressions=max_e+1,statements=0)),
                       ('target_id_s_overflow',capacities(expressions=0,statements=0x1000000)),
                       ('constant_record_size_overflow',capacities(expressions=(1<<64)-1))]:
        req=Requirements();C.memset(C.byref(req),0xa5,C.sizeof(req));before=bytes(req)
        status=query.query(3,C.byref(caps),C.sizeof(caps),C.byref(req),C.sizeof(req))
        errors=[]
        if status!=1 or bytes(req)!=before:errors.append('overflow query did not reject atomically')
        rows.append(dict(case=name,status=status,errors=errors,passed=not errors))
    query.close()
    report=dict(records=rows,passed=sum(r['passed'] for r in rows),failed=sum(not r['passed'] for r in rows))
    write_json(args.out/'results.json',report)
    print(json.dumps(report,indent=2));return int(report['failed']!=0)


if __name__=='__main__':raise SystemExit(main())
