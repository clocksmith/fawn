"""M4 independent black-box runner over unchanged retained API harness."""
from __future__ import annotations
import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import locale
from pathlib import Path
import subprocess
import fixtures
import m4_fixtures
from evaluate import Evaluator
from rational_oracle import self_test


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['root','library','validator','out']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    self_test()
    e=Evaluator(a.library,a.out/'modules',a.validator)
    positives=fixtures.admitted(a.root)+m4_fixtures.admitted(a.root)
    old_negative=[case for case in fixtures.rejected() if case[0] not in m4_fixtures.LIFTS]
    negatives=old_negative+m4_fixtures.rejected()
    def serial(v):
        if isinstance(v,dict):return {str(k):serial(val) for k,val in v.items()}
        if isinstance(v,(tuple,list)):return [serial(x) for x in v]
        if isinstance(v,bytes):return v.decode(errors='replace')
        return v
    (a.out/'frozen-cases.json').write_text(json.dumps(serial(dict(positives=positives,negatives=negatives,lifts=m4_fixtures.LIFTS)),indent=2)+'\n')
    for case in positives:e.valid(*case)
    for case in negatives:e.invalid(*case)
    for name in ['real_scale','real_relu','nodes_64','local_snapshot']:
        e.boundary(next(source for n,source,_ in positives if n==name))
    selected=positives[-106:]+[(n,s,{}) for n,s,_,_ in negatives[-69:]]
    baseline={n:e.call(s) for n,s,_ in selected}
    def parallel(i):
        name,source,_=selected[i%len(selected)];status,r,w,errors=e.call(source);old=baseline[name]
        if status!=old[0] or bytes(r)!=bytes(old[1]) or w!=old[2]:errors.append('parallel nondeterminism '+name)
        return errors
    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        errors=[x for result in pool.map(parallel,range(2800)) for x in result]
    e.record('parallel_m4_2800_16_threads',errors)
    # Set ambient modes only in serial compilation after concurrent tasks settle.
    decimal_cases=[case for case in positives if case[0].startswith('decimal_')]
    saved_locale=locale.setlocale(locale.LC_NUMERIC)
    libc=ctypes.CDLL(None);libc.fegetround.restype=ctypes.c_int;libc.fesetround.argtypes=[ctypes.c_int]
    original_round=libc.fegetround();available=subprocess.check_output(['locale','-a'],text=True).splitlines()
    modes=[];errors=[]
    try:
        for loc in available:
            try:locale.setlocale(locale.LC_NUMERIC,loc)
            except locale.Error:continue
            for rounding in [0,0x400,0x800,0xc00]:
                if libc.fesetround(rounding):continue
                modes.append(dict(locale=loc,rounding=rounding))
                for name,source,_ in decimal_cases:
                    status,r,w,problems=e.call(source);old=baseline[name]
                    errors.extend(problems)
                    if status!=old[0] or bytes(r)!=bytes(old[1]) or w!=old[2]:errors.append('ambient dependence '+name+' '+loc+' '+str(rounding))
    finally:
        libc.fesetround(original_round);locale.setlocale(locale.LC_NUMERIC,saved_locale)
    e.record('decimal_locale_rounding_independence',errors,available_modes=modes,note='Only installed locales exercised; no locale installation.')
    report=dict(positives=len(positives),retained_negatives=len(old_negative),new_negatives=len(m4_fixtures.rejected()),lifts=m4_fixtures.LIFTS,checks=len(e.records),passed=sum(r['passed'] for r in e.records),cpu_trials=e.cpu_trials,records=e.records)
    (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['records','lifts']},indent=2))
    return int(report['passed']!=report['checks'])

if __name__=='__main__':raise SystemExit(main())
