"""Keep historical sources/expectations immutable; narrow explicitly frozen lifts."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import sys
import retained_m8 as old
import semantic_m8 as old_semantic
from evaluator_errata_m8 import apply
from compute_oracle_m9 import Module
from semantic_m9 import test_positive,execute_vectors
from semantic_m8 import sha,write_json
from fixtures_m5 import storage
from fixtures_m6 import trial
from api_job_m7 import JobApi,capacities

LIFTS_M4={'trailing_function','unknown_call'}
LIFTS_M8={'excluded_numeric_feature_4','excluded_numeric_feature_5','excluded_numeric_feature_6'}
ORIGINAL_LIFT=old.lift_expected


def lifted(family: str,name: str,source: bytes) -> dict:
    if family!='m4' or name not in LIFTS_M4:return ORIGINAL_LIFT(family,name,source)
    expected=old.metadata_from_frozen_source(family,name,source)
    vectors=[]
    for x in [0,1,3,0xffffffff]:
        for length in [0,1,4,8]:
            if name=='trailing_function':
                initial={(0,0):([3,0xffffffff,17,4]*2)[:length]}
                changes={(0,0):{x:((initial[(0,0)][x] if x<length else 0)+1)&0xffffffff}}
            else:
                initial={(0,0):[3]*length,(0,1):[7]*length,(0,2):[0xa55a36c9]*length}
                changes={(0,2):{0:1}}
            vectors.append(trial(initial,changes,inputs={28:(x,2,3)},synthetic=True))
    expected['vectors']=vectors
    return expected


class LegacyAdapter(Module):
    def interface(self,expected: dict) -> dict:return super().interface(old.normalized(expected))
    def execute(self,buffers: dict,x: int,read_only: set,y: int=37,z: int=91,uniforms: dict|None=None) -> list:
        return super().execute(buffers,{28:(x,y,z)},read_only,uniforms)


def configure() -> None:
    old.Module=Module;old.LegacyAdapter=LegacyAdapter;old.execute_vectors=execute_vectors
    old.NEW_LIFTS['m4']=old.NEW_LIFTS['m4']|LIFTS_M4;old.lift_expected=lifted


def m8_cases(repo: Path) -> tuple[list,list]:
    good,bad=apply(*old_semantic.source_cases(repo,'all'))
    stay=[]
    for name,source,statuses in bad:
        if name not in LIFTS_M8:stay.append((name,source,statuses));continue
        expected=dict(entry='main',n=[1,1,1],bindings=[storage('out',0),storage('src',1,access=1)],structs=[],builtins=[],vectors=[trial({(0,0):[0xa55a36c9]*3,(0,1):[0,1]}, {})])
        good.append((name,source,expected))
    return good,stay


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','library','validator','out']:parser.add_argument('--'+key,type=Path,required=key in {'repo','out'})
    parser.add_argument('--stage',choices=['historical','m8'],default='historical');parser.add_argument('--freeze',action='store_true')
    args=parser.parse_args()
    if args.stage=='historical':
        configure()
        forwarded=['retained_m8','--repo',str(args.repo),'--out',str(args.out)]
        if args.freeze:forwarded+=['--freeze']
        else:forwarded+=['--library',str(args.library),'--validator',str(args.validator)]
        sys.argv=forwarded
        return old.main()
    args.out.mkdir(parents=True,exist_ok=False)
    good,bad=m8_cases(args.repo)
    inventory=[dict(case=n,source_sha256=sha(s),admitted=True,classification='M9_exact_named_lift' if n in LIFTS_M8 else 'retained_M8_positive') for n,s,e in good]+[dict(case=n,source_sha256=sha(s),admitted=False,expected_statuses=st) for n,s,st in bad]
    write_json(args.out/'inventory.json',inventory)
    if args.freeze:
        write_json(args.out/'lifted_expectations.json',{n:e for n,s,e in good if n in LIFTS_M8})
        print(json.dumps(dict(cases=len(inventory),positive=len(good),negative=len(bad),lifts=sorted(LIFTS_M8))));return 0
    api=JobApi(args.library,capacities(tokens=16384,expressions=8192,statements=2048,words=131072,expression_depth=64,control_depth=32))
    directory=args.out/'modules';directory.mkdir();rows=[]
    for name,source,expected in good:
        row,_=test_positive(api,name,source,expected,directory,args.validator);rows.append(row)
        if not row['passed']:print(json.dumps(row),flush=True)
    for name,source,statuses in bad:
        status,result,words,errors=api.call(source)
        if status not in statuses:errors.append('retained M8 rejection status differs')
        row=dict(case=name,status=status,diagnostic=bytes(result.diagnostic.message).decode(errors='replace'),errors=errors,passed=not errors);rows.append(row)
        if errors:print(json.dumps(row),flush=True)
    report=dict(records=rows,passed=sum(r['passed'] for r in rows),failed=sum(not r['passed'] for r in rows),historical_files_changed=False,cpu_invocations=sum(r.get('execution',{}).get('cpu_invocations',0) for r in rows))
    write_json(args.out/'results.json',report);print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2));api.close();return int(bool(report['failed']))


if __name__=='__main__':raise SystemExit(main())
