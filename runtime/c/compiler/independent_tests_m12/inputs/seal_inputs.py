"""Bind exact independently generated sources and executable oracles before M12 code."""
from __future__ import annotations
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parent
REPO=Path('/workspace/shared/doe-c-migration-20261023')
OWNER=REPO/'runtime/c/compiler'
EVIDENCE=Path('/workspace/shared/doe-c-migration-evidence-20261023/milestone12/evaluator')
EXPECTED='f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, obj: object) -> None:
    path.write_text(json.dumps(obj,indent=2,sort_keys=True)+'\n')

assert digest(OWNER/'CONTRACT_12.sha256')==EXPECTED
contract=subprocess.run(['sha256sum','-c','runtime/c/compiler/CONTRACT_12.sha256'],cwd=REPO,capture_output=True,text=True,check=True)
inputs=json.loads((OWNER/'contracts/m12_source_inputs.json').read_text())
for item in inputs['compilerFiles']:
    assert digest(REPO/item['path'])==item['sha256'],item['path']

plan={
 'gate_order':['Fresh strict GCC14 C11 source build','Decisive reversed dynamic vec4 clamp plus unsigned product-overflow dot actual target execution','All frozen semantic/target cases with all negative output modes','Isolated exact/one-short pools and actual target ID/word ledgers','Public ABI consumers, no allocation/export drift, fenv/MXCSR/errno and sanitizer failures','Source-bound M11/M10/historical regressions with exact per-source new admission mappings','Unchanged exact98 source recompile, validate, execute and remaining secondary blockers','Complete stack/path proof, source identity recheck and truthful receipt'],
 'independent_case_policy':'Exact source bytes and mathematical expectations freeze before implementation. Retain every first failure. Any evaluator correction needs separate old/new evidence, not broad weakened negatives.',
 'pools':{name:{'seed':seed,'method':'Hold all other capacities at frozen accepted ABI4 profile. Find smallest successful selected capacity by monotone bounded search <=262144, verify exact success and one-short first-exhausted-pool failure under flags1,2,3,5,6,7. Required outputs and all user buffers remain unpublished/unchanged on failure. This discovers a measurable exact boundary, not expected semantic behavior.'} for name,seed in [('E','runtime_clamp_f32_4'),('Q','module_cross_AF_3'),('K','module_clamp_f32_4'),('L','module_clamp_f32_4'),('I','effects_clamp'),('O','effects_clamp'),('D','runtime_dot_u32_4'),('V','stage_array_cross'),('W','runtime_reversed_clamp_u32_4')]},
 'preserved_pools':['T','S','R','X'],
 'native_targets':['fixed','v2','job','typed'],
 'native_obligations':['unaligned/disjoint/exact caller spans','stale/overlap and output canaries','all output selections still emit','C11 and C++ consumers','seven exports/header hashes unchanged','no library allocation or mutable global','concurrent distinct workspaces','strict FP flags and rounding/traps/exceptions/MXCSR/errno restored on every new error exit','sanitizer source build','complete individual frames, recursive SCCs and combined call-path bounds'],
 'mutations':['wrong scalar/vector return TypeId','signed compare for u32','swapped clamp ordering','direct poison-prone Clamp','dropped or swapped dot lane','missing modulo32','missing AI final range','checked AI product contrary to N1','reversed cross orientation','duplicate argument effect','reload prior argument after later effect','omitted const-bound check','dropped varying-argument uniformity dependency','enum-order import omission','one target ID or word undercount'],
 'target_rows_source':'runtime/c/compiler/MILESTONE_12_LEDGER.md',
 'historical_policy':'Materialize retained source through existing source-bound prepare_retained.py; do not duplicate corpora or alter original tests/receipts. Name each newly admitted negative individually. Exact98 baseline 52 validated/43 executed/9 validation-only/46 rejected. No assumed +17 admission.',
 'runtime_accuracy_source':'https://www.w3.org/TR/2026/CRD-WGSL-20261021/#floating-point-accuracy',
 'scope_limits':['Independent instruction model, not physical GPU qualification','No TSan portability claim','No whole migration or production/default adoption','No settled universal upstream AI cancellation conformance','No broad old corpus negative rewrites','No pool/layout/export increase'],
 'unsealed_supplements_policy':'Only additive independently derived cases may be added after this seal; they must be labelled by timing and never substitute for the sealed gate. Native boundary harness integration can be completed while author works without querying candidate behavior.'
}
save(ROOT/'GATE_PLAN.json',plan)
source_bound={}
for name in ['independent_tests_m11/IMPORT.sha256','independent_tests_m11/CASE_SEAL.sha256','independent_tests_m11/PRE_CANDIDATE_COMPLETE_V2.sha256','independent_tests_m11/native/SOURCE_SEAL.sha256','independent_tests_m10/OBLIGATIONS_M10.sha256','independent_tests_m10/CONCRETE_SOURCE.sha256','independent_tests_m10/historical/PINNED_SOURCE.json','contracts/m12_witnesses.json']:
    source_bound['runtime/c/compiler/'+name]=digest(OWNER/name)
save(ROOT/'HISTORICAL_INPUTS.json',source_bound)
save(ROOT/'BINDING.json',{'time':datetime.now(timezone.utc).isoformat(),'accepted_compiler_baseline':inputs['acceptedBaseline'],'contract_commit':'da9b75a09e93248f52749d5aa7dd414f4d8a0016','contract_tree':'6802bf888ceb6a4b3091c60e08c9362889892346','contract_sha256':EXPECTED,'installed_contract_check':contract.stdout.splitlines(),'accepted30_dependency_hashes_verified':True,'candidate_implementation_started':False,'candidate_build_loaded':False,'oracle_math':'Python arbitrary integers/Fraction and accepted directed Decimal enclosures, no host-libm self-comparison','selected_AI_policy':'Exact final signed64 dot; preserve Tint disagreement and CTS coverage gap','preseal_correction':'Generic builtin argument status corrected from an unsealed3 guess to semantic4 by accepted M11 call-owner branch. No candidate observation and no sealed case changed. Original unsealed-v1 sources retained separately.'})
files=sorted(list((ROOT/'frozen').glob('*.wgsl'))+[ROOT/name for name in ['cases.json','numeric_oracle.py','target_numeric.py','target_m12.py','workgroup_m12.py','run_cases.py','freeze_cases.py','prepare_model.py','seal_inputs.py','MODEL_IMPORTS.json','ORACLE_CONTROLS.json','GATE_PLAN.json','HISTORICAL_INPUTS.json','BINDING.json']])
manifest=''.join(digest(path)+'  '+str(path.relative_to(ROOT))+'\n' for path in files)
(ROOT/'CASE_SEAL.sha256').write_text(manifest)
case_data=json.loads((ROOT/'cases.json').read_text())
receipt={'sealed_at':datetime.now(timezone.utc).isoformat(),'case_seal_sha256':digest(ROOT/'CASE_SEAL.sha256'),'files':len(files),'exact_source_cases':len(case_data['cases']),'const_signatures':123,'runtime_signatures':69,'status_counts':dict(Counter(x['expected_status'] for x in case_data['cases'])),'contract_sha256':EXPECTED,'case_root':str(ROOT),'executable_oracle_source_sealed':True,'executed_numeric_self_controls':json.loads((ROOT/'ORACLE_CONTROLS.json').read_text()),'native_execution_gate':'pending candidate handoff; no compiler invoked','implementation_release_condition':'Parent can release author after this exact case/oracle seal; final acceptance remains all listed gates.'}
save(ROOT/'CASE_SEAL_RECEIPT.json',receipt)
EVIDENCE.mkdir(parents=True,exist_ok=True)
for name in ['CASE_SEAL.sha256','CASE_SEAL_RECEIPT.json','BINDING.json','MODEL_IMPORTS.json','HISTORICAL_INPUTS.json','GATE_PLAN.json','ORACLE_CONTROLS.json']:
    (EVIDENCE/name).write_bytes((ROOT/name).read_bytes())
print(json.dumps(receipt,indent=2))
