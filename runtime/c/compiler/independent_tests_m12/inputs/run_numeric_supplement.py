"""Run separately sealed additive finite-edge and AF predicates."""
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from abi_m10 import Compiler
from run_cases import execute,verify

ROOT=Path(__file__).resolve().parent


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();verify()
    for line in (ROOT/'NUMERIC_SUPPLEMENT.sha256').read_text().splitlines():
        expected,name=line.split(None,1);assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected,name
    a.out.mkdir(exist_ok=False);compiler=Compiler(a.library);rows=[]
    for case in json.loads((ROOT/'NUMERIC_SUPPLEMENT.json').read_text())['cases']:
        try:row=execute(compiler,case,a.validator,a.out);row['passed']=True
        except Exception as error:row={'id':case['id'],'passed':False,'error':repr(error)}
        rows.append(row);print(case['id'],row['passed'],row.get('error',''),flush=True)
    (a.out/'results.json').write_text(json.dumps({'cases':rows,'failed':[r for r in rows if not r['passed']]},indent=2,sort_keys=True)+'\n')
    return int(any(not r['passed'] for r in rows))

if __name__=='__main__':raise SystemExit(main())
