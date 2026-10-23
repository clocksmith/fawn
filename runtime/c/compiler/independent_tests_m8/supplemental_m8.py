"""Post-disclosure checks, not represented as preinspection independent cases.

2026-10-23: parent disclosed an author-fixed runtime-count/abstract-left overload
edge, without source or developer test inspection. Dated WGSL 6.1.3 excludes
abstract candidates when an argument is runtime. These exact cases independently
check that requirement; original frozen sources and expectations stay untouched.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
from api_job_m7 import JobApi
from fixtures_scalar_m8 import make
from semantic_m8 import test_positive,sha,write_json,canonical


def cases() -> tuple:
    positive=[
      make('disclosed_runtime_shift_default_i32','let count=1u;let x=1 << count;out[0]=bitcast<u32>(x);',[2]),
      make('disclosed_runtime_shift_i32_annotated','let count=1u;let x:i32=1 << count;out[0]=bitcast<u32>(x);',[2]),
      make('disclosed_runtime_shift_negative','let count=1u;let x=(-1) >> count;out[0]=bitcast<u32>(x);',[0xffffffff]),
      make('disclosed_const_shift_abstract_survives','const count=0u;let x:u32=2147483648 << count;out[0]=x;',[0x80000000]),
      make('disclosed_concrete_u32_shift','let count=1u;let x:u32=2147483648u >> count;out[0]=x;',[0x40000000])]
    negative=[]
    for name,body in [
      ('disclosed_runtime_shift_no_expected_u32','let count=1u;let x:u32=1 << count;'),
      ('disclosed_runtime_shift_i32_range','let count=0u;let x=2147483648 << count;'),
      ('disclosed_runtime_shift_range_not_annotation','let count=0u;let x:u32=2147483648 << count;')]:
        _,source,_=make(name,body,[]);negative.append((name,source,4))
    return positive,negative


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True,type=Path);parser.add_argument('--library',type=Path)
    parser.add_argument('--validator',type=Path);parser.add_argument('--freeze',action='store_true');args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=False);good,bad=cases()
    inventory=dict(provenance='author edge disclosed by parent; no implementation/developer test inspection',
                   positive=[dict(name=n,source=s,source_sha256=sha(s),expected=e) for n,s,e in good],
                   negative=[dict(name=n,source=s,source_sha256=sha(s),status=status) for n,s,status in bad])
    write_json(args.out/'inventory.json',inventory)
    if args.freeze:print(sha((args.out/'inventory.json').read_bytes()));return 0
    if args.library is None or args.validator is None:parser.error('run requires library and validator')
    api=JobApi(args.library);modules=args.out/'modules';modules.mkdir();rows=[]
    for n,s,e in good:r,_=test_positive(api,n,s,e,modules,args.validator);rows.append(r)
    for n,s,wanted in bad:
        status,result,_,errors=api.call(s)
        if status!=wanted:errors.append('runtime overload must reject concrete type or range mismatch')
        rows.append(dict(case=n,status=status,diagnostic=bytes(result.diagnostic.message).decode(errors='replace'),errors=errors,passed=not errors))
    api.close();write_json(args.out/'results.json',dict(records=rows,passed=sum(r['passed'] for r in rows),failed=sum(not r['passed'] for r in rows)))
    print(json.dumps(canonical(rows),indent=2));return int(any(not r['passed'] for r in rows))


if __name__=='__main__':raise SystemExit(main())
