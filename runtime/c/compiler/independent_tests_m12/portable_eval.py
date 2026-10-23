#!/usr/bin/env python3
"""Path-explicit source-only M12 evaluator replay. Default is verification/plan only."""
from __future__ import annotations
import argparse,hashlib,importlib,json,sys
from pathlib import Path

PACKAGE=Path(__file__).resolve().parent
INPUTS=PACKAGE/'inputs'
OLD_REPO=Path('/workspace/shared/doe-c-migration-20261023')
STAGES={'primary':'run_cases','numeric-supplement':'run_numeric_supplement','corrections':'run_exact_corrections','historical576':'run_retained_m10_m12','retained120':'run_retained_m11_m12','witnesses':'run_witnesses','same98':'run_same98_rebound','lowering-ledger':'run_lowering_ledger_v2','parser':'run_parse_probe_v2','abi-targets':'consume_abi_targets'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def verify_package():
 for line in (PACKAGE/'IMPORT.sha256').read_text().splitlines():
  digest,name=line.split(None,1);p=PACKAGE/name
  assert p.is_file() and sha(p)==digest,('package identity',name)
def relocated(path,repo):return repo/Path(path).relative_to(OLD_REPO)
def verify_inputs(repo):
 for name in ['CASE_SEAL.sha256','SUPPLEMENT_SEAL.sha256','NUMERIC_SUPPLEMENT.sha256','REGRESSION_SEAL.sha256','HISTORICAL_LIFTS.sha256','RETAINED_M11_SEAL.sha256','EXACT_CORRECTIONS.sha256','LOWERING_LEDGER.sha256','PARSE_PROBE.sha256','CANDIDATE2_RECHECK.sha256']:
  for line in (INPUTS/name).read_text().splitlines():
   digest,path=line.split(None,1);assert sha(INPUTS/path)==digest,(name,path)
 for original,digest in json.loads((INPUTS/'MODEL_IMPORTS.json').read_text()).items():assert sha(relocated(original,repo))==digest,original
 for name,digest in json.loads((INPUTS/'RETAINED_M11_BINDING.json').read_text())['files'].items():assert sha(repo/'runtime/c/compiler/independent_tests_m11'/name)==digest,name
 same=json.loads((INPUTS/'SAME98_INPUTS_M12.json').read_text());assert sha(repo/'bench/gates/spirv_val_gate.py')==same['consumer_sha256']
 assert sha(relocated(same['accepted_m11_receipt'],repo))==same['accepted_m11_receipt_sha256']
 for row in same['rows']:assert sha(repo/row['source'])==row['source_sha256'],row['source']
def verify_candidate(a):
 assert a.owner and a.manifest and a.manifest_sha256,'Explicit immutable owner, manifest and digest required for execution'
 assert sha(a.manifest)==a.manifest_sha256,'candidate manifest identity'
 for line in a.manifest.read_text().splitlines():
  h,name=line.split(None,1);relative=Path(name).relative_to('runtime/c/compiler');assert sha(a.owner/relative)==h,name
 if a.library:assert a.library_sha256 and sha(a.library)==a.library_sha256,'fresh library identity'
 if a.cli:assert a.cli_sha256 and sha(a.cli)==a.cli_sha256,'fresh CLI identity'
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--repo',type=Path,required=True);p.add_argument('--stage',choices=['verify',*STAGES],default='verify');p.add_argument('--execute',action='store_true');p.add_argument('--owner',type=Path);p.add_argument('--manifest',type=Path);p.add_argument('--manifest-sha256');p.add_argument('--library',type=Path);p.add_argument('--library-sha256');p.add_argument('--cli',type=Path);p.add_argument('--cli-sha256');p.add_argument('--validator',type=Path);p.add_argument('--out',type=Path);p.add_argument('--baseline',type=Path);p.add_argument('--baseline-sha256');p.add_argument('--witness-results',type=Path);p.add_argument('--target-manifest',type=Path)
 a=p.parse_args();a.repo=a.repo.resolve();verify_package();verify_inputs(a.repo)
 plan={'stage':a.stage,'execute':a.execute,'package':str(PACKAGE),'repo':str(a.repo),'input_seal':sha(INPUTS/'CASE_SEAL.sha256'),'candidate_manifest_sha256':a.manifest_sha256,'qualification':'Plan/identity verification only unless --execute. No compiler loaded, target run, build or output created by this default path.'}
 if not a.execute or a.stage=='verify':print(json.dumps(plan,indent=2,sort_keys=True));return 0
 assert a.out and not a.out.exists(),'A fresh output directory is required';a.out=a.out.resolve()
 assert not a.out.is_relative_to(PACKAGE) and not a.out.is_relative_to(a.repo),'Outputs must be outside source/evidence trees'
 assert a.validator and a.validator.is_file(),'Explicit approved installed validator required'
 verify_candidate(a)
 for path in [a.repo/'runtime/c/compiler/independent_tests_m11',a.repo/'runtime/c/compiler/independent_tests_m10/source',a.repo/'runtime/c/compiler/independent_tests_m11/oracle',INPUTS]:sys.path.insert(0,str(path))
 import run_cases
 run_cases.verify=lambda:verify_inputs(a.repo)
 original_execute=run_cases.execute
 fixes={f['id']:f for f in json.loads((INPUTS/'EXACT_EVALUATOR_CORRECTIONS.json').read_text())['corrections']}
 def exact_execute(compiler,case,validator,out):
  fix=fixes.get(case['id'])
  if fix:
   assert sha(INPUTS/case['source'])==fix['source_sha256'] and case['expected_status'] in [fix['original_expected_status'],fix['corrected_expected_status']]
   case=dict(case,expected_status=fix['corrected_expected_status'])
  result=original_execute(compiler,case,validator,out)
  if fix:result['exact_source_bound_evaluator_correction']=fix
  return result
 run_cases.execute=exact_execute
 import witness_inputs
 witness_inputs.REPO=a.repo
 module=importlib.import_module(STAGES[a.stage]);args=['--validator',str(a.validator),'--out',str(a.out)]
 if a.stage=='abi-targets':
  assert a.target_manifest;args+=['--manifest',str(a.target_manifest)]
 elif a.stage=='same98':
  assert a.cli and a.witness_results
  meta=a.out.parent/(a.out.name+'-relocation');meta.mkdir(exist_ok=False)
  old=INPUTS/'SAME98_INPUTS_M12.json';data=json.loads(old.read_text());data['accepted_m11_receipt']=str(relocated(data['accepted_m11_receipt'],a.repo));new=meta/old.name;new.write_text(json.dumps(data,indent=2,sort_keys=True)+'\n');module.ROOT=meta
  (meta/'RELOCATION.json').write_text(json.dumps({'original_sha256':sha(old),'relocated_sha256':sha(new),'only_change':'accepted_m11_receipt path prefix','repo':str(a.repo)},indent=2)+'\n')
  args+=['--repo',str(a.repo),'--cli',str(a.cli),'--witness-results',str(a.witness_results)]
 else:
  assert a.library;args+=['--library',str(a.library)]
  if a.stage in ['historical576','retained120']:args+=['--repo',str(a.repo)]
  if a.stage=='parser':
   assert a.baseline and a.baseline_sha256 and sha(a.baseline)==a.baseline_sha256,'Explicit source-bound accepted M11 baseline artifact identity required';args+=['--baseline',str(a.baseline)]
 sys.argv=[str(INPUTS/(STAGES[a.stage]+'.py')),*args]
 result=module.main();verify_candidate(a);verify_package();verify_inputs(a.repo)
 (a.out/'PORTABLE_INVOCATION.json').write_text(json.dumps(dict(plan,execute=True,arguments=args,library_sha256=a.library_sha256,cli_sha256=a.cli_sha256,validator_sha256=sha(a.validator),exit_code=result,qualification='Path relocation and the already-adjudicated exact source-status corrections only; original sealed input bytes remain immutable.'),indent=2,sort_keys=True)+'\n')
 return result
if __name__=='__main__':raise SystemExit(main())
