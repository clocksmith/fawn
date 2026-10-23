"""Historical fixtures stay immutable; exact named M8 lifts have positive oracles."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import struct
import subprocess

import all_fixtures
import fixtures_m5
import fixtures_m6
import fixtures_m7
import retained_m7
from api_job_m7 import JobApi
from api_m5 import Api
from evaluate import Evaluator
from compute_oracle_m8 import Module
from exact_numeric_m8 import fp_operation,literal_bits,round_bits
from semantic_checks import run_vectors as old_storage_vectors
from semantic_m5 import execute_vectors as old_uniform_vectors
from semantic_m8 import execute_vectors,sha,write_json,canonical
from fixtures_m5 import storage,uniform,shape,packed
from fixtures_m6 import trial
from spirv_oracle_base import ExecutionError,StructuralError,OracleLimit

NEW_LIFTS={
 'm4':{'literal_1','literal_0x1u','compound_assignment','reversed_declarations',
       'constant_missing_suffix','constant_inferred_type','constant_expression','second_constant',
       'constant_after_storage','constant_after_function','constant_in_function','constant_used_workgroup',
       'invalid_condition_6','invalid_condition_10','invalid_condition_11',
       'invalid_statement_0','invalid_statement_2','invalid_statement_5','invalid_statement_8',
       'nested_shadow','resource_shadow','entry_shadow','constant_shadow','cast','max_trailing'} |
       {'constant_float_subtree_'+str(i) for i in range(10)} |
       {'float_token_'+str(i) for i in [0,1,2,3,4,6,7,12]} |
       {'unsupported_op_'+str(i) for i in range(7)},
 'm5':{'member_local_shadow','type_local_shadow'},
 'm6':{'comparison_as_value','logical_condition','unsupported_division'},
 'm7':{'inferred_var_abstract','inferred_let_abstract','abstract_addition_subtree','abstract_max_subtree',
       'float_constant_compare','float_suffixed_compare','mixed_abstract_float_compare',
       'float_constant_arithmetic','abstract_dead_subtree','dead_float_constant_compare',
       'unsupported_local_const','unsupported_division'}
}
OLD_LIFTS={'m4':set(retained_m7.M4_LIFTS),'m5':set(retained_m7.M5_LIFTS),'m6':set(),'m7':set()}


def families(repo: Path) -> list:
    return [('m4',all_fixtures.admitted(repo),all_fixtures.rejected()),
            ('m5',fixtures_m5.admitted(repo),fixtures_m5.rejected()),
            ('m6',fixtures_m6.positives(repo),fixtures_m6.negatives()),
            ('m7',fixtures_m7.positives(),fixtures_m7.negatives())]


def metadata_from_frozen_source(family: str,name: str,source: bytes) -> dict:
    if family=='m5':return dict(retained_m7.lifted_expected('m5',name),builtins=[28])
    text=source.decode()
    bindings=[]
    pattern=r'@group\((\d+)\)\s*@binding\((\d+)\)\s*var<storage,\s*(read_write|read)>\s*(\w+)\s*:\s*array<(u32|f32)>'
    for g,b,access,n,scalar in re.findall(pattern,text):
        bindings.append(storage(n,int(b),1 if scalar=='u32' else 2,1 if access=='read' else 2,int(g)))
    entry=re.search(r'\bfn\s+(\w+)\s*\(',text)[1]
    size=re.search(r'@workgroup_size\(([^)]*)\)',text)[1]
    dims=[int(x.strip()) if x.strip()!='delta' else 7 for x in size.split(',') if x.strip()]
    dims += [1]*(3-len(dims))
    return dict(entry=entry,n=dims,bindings=bindings,structs=[],builtins=[28])


def lift_expected(family: str,name: str,source: bytes) -> dict:
    expected=metadata_from_frozen_source(family,name,source)
    vectors=[]
    for x in ([1,3,U] if name=='invalid_statement_8' else [0,1,3,U]):
        for length in [0,1,4,8]:
            initial={}
            for b in expected['bindings']:
                if b['kind']!=1:continue
                pattern=([0x3f800000,0x40000000,0xbf800000,0x40400000] if b['scalar']==2
                         else [3,U,17,4])
                initial[(b['group'],b['binding'])]=([S]*length if b['name'] in {'out','fout'} else (pattern*2)[:length])
            want={k:list(v) for k,v in initial.items()};uniforms={}
            if family=='m5':uniforms={(0,0):packed([7,0x3f800000,11])}
            def put(key: tuple,index: int,value: int) -> None:
                if 0<=index<len(want[key]):want[key][index]=value&U
            def load(key: tuple,index: int) -> int:
                return initial[key][index] if 0<=index<len(initial[key]) else 0
            if family=='m4':
                if name in {'literal_1','literal_0x1u','compound_assignment','reversed_declarations','second_constant'} or name.startswith('constant_') and name in NEW_LIFTS['m4'] and not name.startswith('constant_float') and name!='constant_shadow':
                    increment=1 if name in {'literal_1','literal_0x1u','compound_assignment','reversed_declarations'} else 7
                    put((0,0),x,load((0,0),x)+increment)
                elif name.startswith('constant_float_subtree_'):
                    i=int(name.rsplit('_',1)[1]);a=load((0,0),x)
                    fixed=[literal_bits('0.3'),literal_bits('0.3'),literal_bits('0.5'),literal_bits('6.0'),literal_bits('2.0'),literal_bits('2.0')]
                    if i<6:value=fixed[i]
                    elif i==6:value=fp_operation('*',a,literal_bits('0.3'))
                    elif i==7:value=a
                    elif i==8:value=fp_operation('max',a,literal_bits('2.0'))
                    else:value=fp_operation('+',a,literal_bits('2.0'))
                    put((0,2),x,value)
                elif name.startswith('float_token_'):
                    value={0:'0.5',1:'5.0',2:'100.0',3:'100.0',4:'2.0',6:'123456789.0',7:'0.000000001',12:'-1.0'}[int(name.rsplit('_',1)[1])]
                    put((0,2),x,literal_bits(value))
                elif name.startswith('unsupported_op_'):
                    a=load((0,0),x);i=int(name.rsplit('_',1)[1])
                    value=[a//2,a%2,a&2,a|2,a^2,(a<<2)&U,(~a)&U][i]
                    put((0,2),x,value)
                elif name=='cast':put((0,2),0,1)
                elif name=='max_trailing':put((0,2),0,2)
                elif name=='invalid_condition_6' and x==1:put((0,2),0,1)
                elif name=='invalid_condition_11' and x in {0,1}:put((0,2),0,1)
                elif name=='invalid_statement_0' and x==0:put((0,2),0,1)
                elif name not in {'invalid_condition_6','invalid_condition_10','invalid_condition_11','invalid_statement_0',
                                   'invalid_statement_2','invalid_statement_5','invalid_statement_8','nested_shadow',
                                   'resource_shadow','entry_shadow','constant_shadow'}:
                    raise ValueError('unmodelled named historical lift '+name)
            elif family=='m5':put((0,1),0,1)
            elif name=='unsupported_division':put((0,0),0,x//2 if family=='m6' else 2)
            v=trial(initial,{},inputs={28:(x,2,3)},uniforms=uniforms,synthetic=True);v['want']=want;vectors.append(v)
    expected['vectors']=vectors
    return expected


U=0xffffffff;S=0xa55a36c9


def normalized(expected: dict) -> dict:
    exp=dict(expected)
    if isinstance(exp['n'],int):exp['n']=[exp['n'],1,1]
    exp.setdefault('structs',[]);exp.setdefault('builtins',[28])
    exp['bindings']=[b if 'kind' in b else storage(b['name'],b['binding'],b['scalar'],b['access'],b['group']) for b in exp['bindings']]
    return exp


class LegacyAdapter(Module):
    def interface(self,expected: dict) -> dict:
        return super().interface(normalized(expected))

    def execute(self,buffers: dict,x: int,read_only: set,y: int=37,z: int=91,uniforms: dict | None=None) -> list:
        return super().execute(buffers,{28:(x,y,z)},read_only,uniforms)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','library','validator','out']:
        parser.add_argument('--'+name,type=Path,required=name in {'repo','out'},help='Explicit '+name+'.')
    parser.add_argument('--freeze',action='store_true',help='No candidate reads; record exact classifications.')
    parser.add_argument('--family',choices=['m4','m5','m6','m7'],action='append',help='Focused retained family.')
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    families_to_run=families(args.repo)
    if args.family:families_to_run=[f for f in families_to_run if f[0] in args.family]
    inventory=[]
    for family,good,bad in families_to_run:
        for name,source,_ in good:inventory.append(dict(family=family,case=name,source_sha256=sha(source),admitted=True,provenance='retained_positive'))
        for name,source,*_ in bad:
            lifted=name in NEW_LIFTS[family]|OLD_LIFTS[family]
            inventory.append(dict(family=family,case=name,source_sha256=sha(source),admitted=lifted,
                                  provenance='new_exact_lift' if name in NEW_LIFTS[family] else 'prior_exact_lift' if lifted else 'retained_negative'))
    write_json(args.out/'inventory.json',inventory)
    if args.freeze:
        expectations={f+'_'+n:lift_expected(f,n,s) for f,good,bad in families_to_run for n,s,*_ in bad if n in NEW_LIFTS[f]}
        write_json(args.out/'new_lift_expectations.json',expectations)
        print(json.dumps(dict(cases=len(inventory),new_lifts=len(expectations),inventory_sha256=sha((args.out/'inventory.json').read_bytes()))));return 0
    if args.library is None or args.validator is None:parser.error('run requires library and validator')
    fixed=Api(args.library);job=JobApi(args.library);abi1=Evaluator(args.library,args.out/'abi1',args.validator)
    modules=args.out/'modules';modules.mkdir();records=[]
    retained_m7.ComputeModule=Module
    for family,good,bad in families_to_run:
        adapter=job if family=='m7' else fixed
        for name,source,expected in good:
            status,result,words,errors=adapter.call(source)
            detail=dict(family=family,case=name,source_sha256=sha(source),status=status,diagnostic=bytes(result.diagnostic.message).decode(errors='replace'))
            if status:errors.append('retained positive rejected')
            else:
                path=modules/(family+'_'+name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
                val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
                if val.returncode:errors.append(val.stderr)
                try:
                    if family in {'m4','m5'}:
                        module=LegacyAdapter(words);module.interface(expected)
                        detail['cpu_invocations']=(old_storage_vectors if family=='m4' else old_uniform_vectors)(module,expected)
                    else:
                        module=Module(words);module.interface(expected)
                        detail.update(execute_vectors(module,expected))
                except (ExecutionError,StructuralError,OracleLimit,ValueError,KeyError,IndexError,AssertionError,TypeError) as exc:errors.append(type(exc).__name__+': '+str(exc))
            if family=='m4':
                other_status,_,other,problems=abi1.call(source);errors+=problems
                if other_status or other!=words:errors.append('retained current ABI1/v2 words differ')
            detail.update(errors=errors,passed=not errors);records.append(detail)
            if errors:print(json.dumps(detail),flush=True)
        for name,source,*rest in bad:
            status,result,words,errors=adapter.call(source)
            lifted=name in NEW_LIFTS[family]|OLD_LIFTS[family]
            detail=dict(family=family,case=name,source_sha256=sha(source),status=status,
                        classification='new_lift' if name in NEW_LIFTS[family] else 'old_lift' if lifted else 'negative',
                        diagnostic=bytes(result.diagnostic.message).decode(errors='replace'))
            if lifted:
                if status:errors.append('exact named lift rejected')
                else:
                    path=modules/(family+'_lift_'+name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
                    val=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True,check=False)
                    if val.returncode:errors.append(val.stderr)
                    try:
                        if name in OLD_LIFTS[family]:detail['cpu_invocations']=retained_m7.execute_lift(words,family,name,result.reflection)
                        else:
                            expected=lift_expected(family,name,source);module=Module(words);module.interface(expected)
                            detail.update(execute_vectors(module,expected))
                    except (ExecutionError,StructuralError,OracleLimit,ValueError,KeyError,IndexError,AssertionError,TypeError) as exc:errors.append(type(exc).__name__+': '+str(exc))
            else:
                if not status:errors.append('malformed/type-invalid/excluded historical source was accepted')
                if family=='m4' and rest[-1] is not None and status!=rest[-1]:errors.append('retained fixed capacity status differs')
                if family=='m7':
                    expected_status=rest[0]
                    if name in {'abstract_subtraction_subtree','unsupported_unary_minus','unsuffixed_index_signed_range'}:expected_status=4
                    permitted=expected_status if isinstance(expected_status,(tuple,list)) else (expected_status,)
                    if status not in permitted:errors.append('retained M7 semantic/syntax category differs')
            detail.update(errors=errors,passed=not errors);records.append(detail)
            if errors:print(json.dumps(detail),flush=True)
    report=dict(records=records,passed=sum(r['passed'] for r in records),failed=sum(not r['passed'] for r in records),
                cpu_invocations=sum(r.get('cpu_invocations',0) for r in records),historical_files_changed=False)
    write_json(args.out/'results.json',report)
    print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2));fixed.close();job.close()
    return int(report['failed']!=0)


if __name__=='__main__':raise SystemExit(main())
