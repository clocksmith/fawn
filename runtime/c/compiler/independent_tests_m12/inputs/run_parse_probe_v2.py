from pathlib import Path
import json,hashlib,argparse
from abi_m10 import Compiler,profile
from run_cases import execute
ROOT=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--baseline',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
 for line in (ROOT/'PARSE_PROBE.sha256').read_text().splitlines():
  expected,name=line.split(None,1);assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected,name
 rows=[]
 for case in json.loads((ROOT/'PARSE_PROBE.json').read_text())['cases']:
  fixes=json.loads((ROOT/'PARSE_PROBE_CORRECTION.json').read_text())['changes']
  for fix in fixes:
   if case['id']==fix['id']:
    assert case['source_sha256']==fix['source_sha256'] and case['expected_status']==fix['old_status']
    case=dict(case,expected_status=fix['new_status'])
  try:
   caps=profile();caps.type_syntax_depth=case.get('type_depth',64);c=Compiler(a.library,caps);row=execute(c,case,a.validator,a.out)
   if 'expected_pool' in case:
    for mode in [1,2,3,5,6,7]:
     s,r,_=c.call((ROOT/case['source']).read_text(),mode);assert r.exhausted_pool==case['expected_pool'] and r.minimum_pool_capacity==case['expected_minimum'],(r.exhausted_pool,r.minimum_pool_capacity)
    row['pool']=r.exhausted_pool;row['minimum']=r.minimum_pool_capacity
   if case.get('baseline_preserved'):
    old=a.out/'accepted_m11';old.mkdir(exist_ok=True);oldrow=execute(Compiler(a.baseline),case,a.validator,old);row['accepted_M11_control']=oldrow
   row['passed']=True
  except Exception as e:row={'id':case['id'],'passed':False,'error':repr(e)}
  rows.append(row);print(row['id'],row['passed'],row.get('error',''),flush=True)
 (a.out/'results.json').write_text(json.dumps({'cases':rows,'failed':[r for r in rows if not r['passed']]},indent=2,sort_keys=True)+'\n');return int(any(not r['passed'] for r in rows))
if __name__=='__main__':raise SystemExit(main())
