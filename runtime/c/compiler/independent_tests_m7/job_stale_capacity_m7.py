"""Focused frozen stale-config workspace and reset-free recovery obligation."""
from __future__ import annotations
import argparse
import ctypes as C
import json
from pathlib import Path
from api_job_m7 import JobApi, capacities, EMPTY, INPUT_ERROR, Result, Reflection, U32, SENTINEL

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    api=JobApi(args.library,capacities(tokens=16,statements=0,expressions=0,words=512))
    larger=capacities()
    source=C.create_string_buffer(EMPTY,len(EMPTY))
    words=(U32*512)(*([SENTINEL]*512))
    before_words=bytes(words)
    before_workspace=C.string_at(api.workspace,api.workspace_size)
    result=Result()
    status=api.compile(3,C.byref(larger),C.sizeof(larger),api.workspace,api.workspace_size,source,len(EMPTY),words,512,C.byref(result),C.sizeof(result))
    errors=[]
    if status!=INPUT_ERROR or result.diagnostic.offset!=0:
        errors.append('stale capacity preflight result')
    if bytes(words)!=before_words or C.string_at(api.workspace,api.workspace_size)!=before_workspace:
        errors.append('stale-config preflight modified words/workspace')
    if result.word_count or bytes(result.reflection)!=bytes(C.sizeof(Reflection)):
        errors.append('stale-config failure published result')
    recovery,_,_,guards=api.call(EMPTY)
    if recovery or guards:
        errors.append('original-capacity workspace failed reset-free recovery')
    api.close()
    report=dict(status='FAIL' if errors else 'PASS',stale_status=status,recovery_status=recovery,errors=errors)
    (args.out/'stale-capacity.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
    return int(bool(errors))

if __name__=='__main__':
    raise SystemExit(main())
