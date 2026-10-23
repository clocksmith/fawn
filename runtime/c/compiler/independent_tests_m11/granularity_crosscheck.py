"""Execute the pre-candidate frozen small-fixture instruction/event cross-check."""
from pathlib import Path
import hashlib
import json
import sys
import run_cases

HERE=Path(__file__).resolve().parent
original=run_cases.dispatch

def event_dispatch(*args,**kwargs):
    kwargs['pure_quantum']=64
    return original(*args,**kwargs)

if __name__=='__main__':
    run_cases.dispatch=event_dispatch
    result=run_cases.main()
    out=Path(sys.argv[sys.argv.index('--out')+1])
    (out/'QUANTUM_OVERRIDE.json').write_text(json.dumps({'reason':'Frozen SCHEDULING_SUPPLEMENT.md requires actual small-fixture instruction/event granularity cross-check; original run used quantum1. Only quantum is overridden to64; sources, inputs, expected bytes, schedules, dispatch counts and assertions are unchanged.','quantum':64,'runner_sha256':hashlib.sha256((HERE/'run_cases.py').read_bytes()).hexdigest(),'wrapper_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()},indent=2)+'\n')
    raise SystemExit(result)
