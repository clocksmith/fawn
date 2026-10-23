from pathlib import Path
import argparse,hashlib,json
from abi_m10 import Compiler
from run_cases import execute,verify
ROOT=Path(__file__).resolve().parent

def main():
 p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();verify()
 for line in (ROOT/'EXACT_CORRECTIONS.sha256').read_text().splitlines():
  expected,name=line.split(None,1);assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==expected,name
 data=json.loads((ROOT/'EXACT_EVALUATOR_CORRECTIONS.json').read_text());a.out.mkdir(exist_ok=False);c=Compiler(a.library);rows=[]
 for fix in data['corrections']:
  assert hashlib.sha256((ROOT/fix['source']).read_bytes()).hexdigest()==fix['source_sha256']
  case=dict(id=fix['id'],source=fix['source'],expected_status=fix['corrected_expected_status'])
  try:row=execute(c,case,a.validator,a.out);row['passed']=True
  except Exception as e:row={'id':case['id'],'passed':False,'error':repr(e)}
  rows.append(row)
 for case in data['additional_cases']:
  try:row=execute(c,case,a.validator,a.out);row['passed']=True
  except Exception as e:row={'id':case['id'],'passed':False,'error':repr(e)}
  rows.append(row)
 (a.out/'results.json').write_text(json.dumps({'cases':rows,'all_passed':all(r['passed'] for r in rows),'original_failure_receipts':data['original_results']},indent=2,sort_keys=True)+'\n');print(json.dumps(rows,indent=2));return int(not all(r['passed'] for r in rows))
if __name__=='__main__':raise SystemExit(main())
