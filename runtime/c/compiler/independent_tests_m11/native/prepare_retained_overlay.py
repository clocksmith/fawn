#!/usr/bin/env python3
"""Only exact previously accepted M10 historical overlays, before M11 inspection."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
REPO=Path('/workspace/shared/doe-c-migration-20261023')
OLD=REPO/'runtime/c/compiler/independent_tests_m9/native_capacity/source'
M10=REPO/'runtime/c/compiler/independent_tests_m10/candidate-v6/native-retained'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 plan=json.loads((HERE/'HISTORICAL_MAPPING_BEFORE_ADAPTERS.json').read_text());binding=json.loads((M10/'RETAINED_INPUTS.json').read_text())
 for name,h in binding['files'].items():
  if sha(REPO/name)!=h:raise RuntimeError('Changed historical source '+name)
 out=HERE/'retained_overlay';out.mkdir(exist_ok=False);identities={};changes=[]
 names=['host_support_m9.h','fixtures_m9.h','host_controls_m9.c','host_environment_m9.c','public_exits_m9.c','native_m9.cpp','native_job_m9.c','fenv_faults_m9.c','native_m6_m9_boundary.c','native_m5.c']
 for n in names:
  p=REPO/'runtime/c/compiler/independent_tests_m7/native_m5.c' if n=='native_m5.c' else OLD/n
  identities[n]={'original':str(p),'original_sha256':sha(p)};(out/n).write_bytes(p.read_bytes())
 for a in plan['accepted_M10_retained_adapters_reused_without_semantic_change']:
  for name in a.get('files',[a.get('file')]):
   p=out/name;s=p.read_text();old=a.get('old',a.get('exact_old'));new=a.get('new',a.get('exact_new'))
   if s.count(old)!=1:raise RuntimeError('Exact replacement mismatch '+name+' '+old)
   p.write_text(s.replace(old,new));changes.append({'file':name,'old':old,'new':new})
 for n in names:identities[n]['overlay_sha256']=sha(out/n)
 result={'candidate_inspected':False,'mapping_sha256':sha(HERE/'HISTORICAL_MAPPING_BEFORE_ADAPTERS.json'),'original_files_modified':False,'identities':identities,'changes':changes,'new_M11_semantic_adapters':[]}
 (out/'OVERLAY_IDENTITY.json').write_text(json.dumps(result,indent=2)+'\n')
 print('Prepared exact accepted M10 historical overlays only; no compiler loaded')
if __name__=='__main__':main()
