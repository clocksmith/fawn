"""Current portable source-only M9 native/API/capacity/CLI acceptance front door.

Original fixture/oracle seals remain immutable. Precisely documented repairs:
M7/M6 private workspace assertions use the reviewed122880 ceiling; all >255-formal
fixtures use the already-sealed validator argument resource option; qualifying
large-call validation runs first with1800s instead of waiting on optional default
resource-limit validation. No WGSL, expected status or semantic assertion changes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source-root',required=True,type=Path)
    p.add_argument('--retained-root',required=True,type=Path)
    p.add_argument('--validator',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--prebuilt',type=Path)
    p.add_argument('--skip-sanitizer',action='store_true')
    p.add_argument('--compact',action='store_true',help='Optional guarded low-disk local build, ordinary portable route is default')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    seal=json.loads((HERE/'CURRENT_SOURCE_MANIFEST.json').read_text())
    for name,expected in seal['files'].items():
        if hashlib.sha256((HERE/name).read_bytes()).hexdigest()!=expected:raise RuntimeError('current evaluator source modified: '+name)
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH=str(a.retained_root.resolve()/'runtime/c/compiler/independent_tests_m7'))
    common=['--source-root',str(a.source_root.resolve()),'--retained-root',str(a.retained_root.resolve())]
    optional=['--skip-sanitizer'] if a.skip_sanitizer else []
    prefix=[sys.executable,str(HERE/'guarded_final_m9.py'),'native'] if a.compact else [sys.executable,str(HERE/'run_native_m9_final.py')]
    native=[*prefix,*common,'--out',str(a.out/'native'),*optional]
    if a.prebuilt:native+=['--prebuilt',str(a.prebuilt.resolve())]
    cli_prefix=[sys.executable,str(HERE/'guarded_final_m9.py'),'cli'] if a.compact else [sys.executable,str(HERE/'run_cli_numeric_m9.py')]
    commands=[('native',native),('capacity',[sys.executable,str(HERE/'run_capacity_m9_final.py'),'--library',str(a.out/'native/build/libm8-source.so'),'--validator',str(a.validator.resolve()),'--out',str(a.out/'capacity')]),('cli',[*cli_prefix,*common,'--validator',str(a.validator.resolve()),'--out',str(a.out/'cli'),*optional])]
    rows=[]
    for name,cmd in commands:
        with (a.out/(name+'.log')).open('w') as log:r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,check=False)
        rows.append(dict(name=name,command=cmd,returncode=r.returncode));print(name,r.returncode,flush=True)
        (a.out/'frontdoor.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
        if r.returncode==73:break
    return any(row['returncode'] for row in rows)

if __name__=='__main__':raise SystemExit(main())
