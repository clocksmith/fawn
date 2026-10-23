"""Portable source-only M9 native/capacity/CLI front door; no Zig or downloads."""
from __future__ import annotations
import argparse
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
    a=p.parse_args(); a.out.mkdir(parents=True,exist_ok=False)
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH=str(a.retained_root.resolve()/'runtime/c/compiler/independent_tests_m7'))
    common=['--source-root',str(a.source_root.resolve()),'--retained-root',str(a.retained_root.resolve())]
    optional=['--skip-sanitizer'] if a.skip_sanitizer else []
    native=[sys.executable,str(HERE/'run_native_m9_reviewed.py'),*common,'--out',str(a.out/'native'),*optional]
    if a.prebuilt: native+=['--prebuilt',str(a.prebuilt.resolve())]
    commands=[('native',native),('capacity',[sys.executable,str(HERE/'capacity_m9.py'),'--library',str(a.out/'native/build/libm8-source.so'),'--validator',str(a.validator.resolve()),'--out',str(a.out/'capacity')]),('cli',[sys.executable,str(HERE/'run_cli_numeric_m9.py'),*common,'--validator',str(a.validator.resolve()),'--out',str(a.out/'cli'),*optional])]
    rows=[]
    for name,cmd in commands:
        with (a.out/(name+'.log')).open('w') as log:
            r=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,check=False)
        rows.append(dict(name=name,command=cmd,returncode=r.returncode)); print(name,r.returncode,flush=True)
        (a.out/'frontdoor.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
    return any(row['returncode'] for row in rows)

if __name__=='__main__': raise SystemExit(main())
