"""Revalidate exact targets; carry prior full dispatches only on byte identity."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys
from abi_m10 import Compiler
from run_cases import verify
from run_witnesses import verify_supplement
from witness_inputs import cases,ROOT,REPO

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();verify();verify_supplement();a.out.mkdir(exist_ok=False)
 binding=json.loads((ROOT/'CANDIDATE2_RECHECK_BINDING.json').read_text());prior_path=Path(binding['prior_witness_results']);assert sha(prior_path)==binding['prior_witness_results_sha256'];prior={c['id']:c for c in json.loads(prior_path.read_text())['cases']};sealed={c['id']:c for c in json.loads((ROOT/'WITNESS_CASES.json').read_text())['rows']};compiler=Compiler(a.library);rows=[]
 for case in cases():
  row={'id':case['id'],'source':case['source'],'passed':False,'executed':False,'newly_executed':False,'carried_forward_execution':False}
  try:
   source=(REPO/case['source']).read_bytes();assert hashlib.sha256(source).hexdigest()==sealed[case['id']]['source_sha256']
   for key,value in case['buffers'].items():assert hashlib.sha256(value).hexdigest()==sealed[case['id']]['buffers'][str(key)]['sha256']
   s,r,_=compiler.call(source.decode(),7);assert s==0,(s,bytes(r.diagnostic.message));s,r,arr=compiler.call(source.decode(),3,r.required.fields());assert s==0;words=list(arr['words']);raw=struct.pack('<'+'I'*len(words),*words);target=a.out/(case['id']+'.spv');target.write_bytes(raw)
   result=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True);assert result.returncode==0,result.stderr
   row.update(validated=True,target_sha256=sha(target));old=prior[case['id']]
   if old['passed'] and old['executed'] and len(old['dispatches'])==8 and old['target_sha256']==row['target_sha256']:
    row.update(passed=True,executed=True,carried_forward_execution=True,prior_receipt_sha256=binding['prior_witness_results_sha256'],prior_source=old['source'],dispatches=old['dispatches'],qualification='Fresh compile/validation, exact target byte identity to candidate1 complete8-dispatch proof; no newly executed candidate2 dispatch')
   else:
    child=a.out/(case['id']+'-fresh');command=[sys.executable,str(ROOT/'run_witnesses.py'),'--library',str(a.library),'--validator',str(a.validator),'--out',str(child),'--select',case['id']];result=subprocess.run(command);proof=json.loads((child/'results.json').read_text());assert len(proof['cases'])==1;fresh=proof['cases'][0];assert result.returncode==0 and fresh['passed'] and fresh['target_sha256']==row['target_sha256'];row.update(fresh,newly_executed=True,carried_forward_execution=False)
  except Exception as e:row['error']=repr(e)
  rows.append(row);print(case['id'],row['passed'],'new',row['newly_executed'],'carried',row['carried_forward_execution'],row.get('error',''),flush=True)
  report={'candidate_library_sha256':sha(a.library),'cases':rows,'failed':[r for r in rows if not r['passed']],'newly_executed_sources':sum(r['newly_executed'] for r in rows),'carried_forward_sources':sum(r['carried_forward_execution'] for r in rows)};(a.out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
 return int(bool(report['failed']))
if __name__=='__main__':raise SystemExit(main())
