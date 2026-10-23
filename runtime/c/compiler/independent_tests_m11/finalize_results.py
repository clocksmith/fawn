"""Fail-closed collation; never reruns or substitutes for a pending target proof."""
from pathlib import Path
from datetime import datetime, timezone
import copy
import hashlib
import json

HERE=Path(__file__).resolve().parent

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def read(path):
    return json.loads(path.read_text())

def verify_manifest(path,root):
    for line in path.read_text().splitlines():
        expected,name=line.split(None,1)
        assert sha(root/name)==expected, str(root/name)

def main():
    verify_manifest(HERE/'CASE_SEAL.sha256',HERE)
    verify_manifest(HERE/'PRE_CANDIDATE_COMPLETE_V2.sha256',HERE)
    verify_manifest(HERE/'native/NATIVE_ARTIFACTS.sha256',HERE/'native')
    run=Path((HERE/'SOURCE_RUN_PATH.txt').read_text().strip())
    result=read(run/'cases/results.json')
    frozen=read(HERE/'frozen/cases.json')
    effective={case['id']:copy.deepcopy(case) for case in frozen}
    bounds=read(HERE/'PRE_CANDIDATE_BOUNDS_CORRECTION.json')
    effective[bounds['original_case']].pop('execution',None)
    effective[bounds['new_case']['id']]=bounds['new_case']
    effective.update({case['id']:case for case in read(HERE/'PRE_CANDIDATE_EXECUTION_SUPPLEMENT.json')['cases']})
    actual={case['id']:case for case in result['cases']}
    assert len(actual)==len(result['cases']) and set(actual)==set(effective), ('incomplete case matrix',len(actual),len(effective))
    assert not result['failed'] and not result['pending'] and all(c['outcome']=='passed' for c in actual.values()), result['failed']
    assert result['library_sha256']=='5d301e813223c9602f7c7ad57634b0ad53a68ba8b752066fbfdce1e9f29b2553'
    executions=[]
    for name,case in effective.items():
        if 'execution' not in case:
            continue
        expected=case['execution'];observed=actual[name]['execution']
        assert len(observed)==len(expected.get('schedules',[0,1,2,3]))*expected.get('dispatches',2),name
        if 'oracle_expected_failure' in expected:
            assert all('expected_race_detected' in r for r in observed),name
        else:
            assert all(r['groups']==expected['groups'][0]*expected['groups'][1]*expected['groups'][2] for r in observed),name
            assert all(r['caller_supplied_reusable_shared_storage'] for r in observed),name
            assert [r['poison'] for r in observed]==[165,90]*4,name
        executions.extend(observed)
    crosschecks=[]
    for selection in ('execute_','control_'):
        cross=read(run/('event64-'+selection)/'results.json')
        assert not cross['failed'] and not cross['pending']
        for row in cross['cases']:
            old=actual[row['id']]
            assert row['outcome']=='passed'
            if 'word_sha256' in old:assert row['word_sha256']==old['word_sha256']
            for x,y in zip(old.get('execution',[]),row.get('execution',[])):
                if 'expected_race_detected' in x:assert 'expected_race_detected' in y
                else:assert x['steps']==y['steps'] and x['barriers_per_group']==y['barriers_per_group']
            crosschecks.append(row['id'])
    native=read(HERE/'native/NATIVE_VERDICT.json')
    assert native['status']=='PASS_WITH_PRESERVED_EVALUATOR_CORRECTIONS'
    assert not native['native_gates_unqualified'] and not native['candidate_defects_observed']
    inherited=read(run/'m10-regression/results.json')
    assert inherited['failed']==['private']
    correction=read(HERE/'POST_CANDIDATE_PRIVATE_STATUS_MAPPING.json')
    assert correction['M11_status']==3 and correction['accepted_M10_status']==2
    for control in ('ORACLE_PREPARATION_V3.json','ORACLE_PHASE_CONTROLS_V3.json','ORACLE_MEMORY_CONTROLS.json','ORACLE_ZERO_CONTROLS.json','ORACLE_SKIP_CONTROLS.json','ORACLE_GRAPH_CONTROLS.json','quantum1-controls/self_controls.json','quantum1-controls/phase_controls.json'):
        assert read(HERE/control)['passed'],control
    out=HERE/'candidate2-observations';out.mkdir(exist_ok=True)
    full=out/'SOURCE_BUILT_CASES_FINAL.json';full.write_bytes((run/'cases/results.json').read_bytes())
    for selection in ('execute_','control_'):
        (out/('GRANULARITY_'+selection.rstrip('_').upper()+'.json')).write_bytes((run/('event64-'+selection)/'results.json').read_bytes())
    corpus=read(run/'same98/results.json')
    assert corpus['failed']==['bench/kernels/workgroup_non_atomic_1024x100.wgsl']
    witnesses={}
    for case in frozen:
        if 'original_source_path' in case:witnesses.setdefault(case['original_source_path'],[]).append(case['id'])
    for row in corpus['records']:
        if row['source'] in witnesses:
            names=witnesses[row['source']]
            assert all(actual[n]['word_sha256']==row['target_sha256'] and actual[n].get('execution') for n in names)
            assert row['errors'] in ([],['mandatory CLI target lacks exact-hash independently executed proof'])
            row['errors']=[];row['executed']=True
            row['execution']={'reuse_exact_target_proofs':names,'receipt_path':str(full),'receipt_sha256':sha(full)}
    corpus['failed']=[r['source'] for r in corpus['records'] if r['errors']]
    assert not corpus['failed']
    corpus['counts']={k:sum(f(r) for r in corpus['records']) for k,f in {
        'paths':lambda r:1,'emitted':lambda r:r['emitted'],'validated':lambda r:r['validated'],
        'executed':lambda r:r['executed'],'rejected':lambda r:not r['emitted']}.items()}
    corpus['reconciliation']={'method':'No emission or validation rerun. Only the pending exact-target execution proof is closed against the original fresh CLI target hashes; every mandatory proof is rebound to the final preserved source-built receipt.','original_receipt':str(out/'SAME98_PENDING_LONG_GATE.json'),'original_sha256':sha(out/'SAME98_PENDING_LONG_GATE.json')}
    (out/'SAME98_FINAL.json').write_text(json.dumps(corpus,indent=2)+'\n')
    stats={'execution_scenarios':sum('execution'in c for c in effective.values()),'dispatches':len(executions),'successful_dispatches':sum('steps'in r for r in executions),'expected_race_failures':sum('expected_race_detected'in r for r in executions),'instructions':sum(r.get('steps',0) for r in executions),'events':sum(r.get('events',0) for r in executions),'memory_accesses':sum(r.get('memory_accesses',0) for r in executions)}
    verdict={'utc':datetime.now(timezone.utc).isoformat(),'status':'PASS_WITH_PRESERVED_EVALUATOR_CORRECTIONS','candidate':'M11 candidate2','source_manifest_sha256':'b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af','source_archive_sha256':'c3ddb0594195aff3d157f7ee5cc488c4df7289f6c91f6b2dbe49c0302ba6172a','case_seal_sha256':sha(HERE/'CASE_SEAL.sha256'),'pre_candidate_supplement_seal_sha256':sha(HERE/'PRE_CANDIDATE_COMPLETE_V2.sha256'),'native_seal_sha256':sha(HERE/'native/NATIVE_ARTIFACTS.sha256'),'effective_cases':len(actual),'execution':stats,'granularity_crosscheck_cases':crosschecks,'explicit_graph_cases':len(read(HERE/'ORACLE_GRAPH_CONTROLS.json')['checks']),'same98':corpus['counts'],'new_validated_only':[r['source'] for r in corpus['records'] if r['validated'] and not r['executed']],'retained_M10_cases':len(inherited['cases']),'retained_M10_expectation_accounting':{'unchanged_accepted_M10_expectations':len(inherited['cases'])-2,'presealed_workgroup_admission_lift':1,'exact_contract_private_status_mapping':1},'retained_M10_exact_mappings':['workgroup: accepted-M10 PARSE_ERROR2 -> admitted0, pre-candidate seal','private: accepted-M10 PARSE_ERROR2 -> UNSUPPORTED3, exact frozen-contract mapping after preserving original failure'],'candidate_defects_observed':[],'unqualified_required_gates':[],'qualifications':['Standalone compiler, independent CPU target model and default Vulkan1.1 validator only.','GCC14.2/Linux x86-64 native ABI and C-owned stack bounds; external/system/caller frames excluded.','Nine additional new same98 emissions are validation-only, not instruction-executed acceptance.','No GPU, runtime metadata adoption, Vulkan/Metal/D3D12 integration, hardware behavior, performance parity or whole-Doe migration acceptance.','Full-capacity nonoverlap is a caller precondition. No TSan or alternate-host-ABI claim.'],'long_case':actual['witness_nonatomic_100']}
    (HERE/'VERDICT.json').write_text(json.dumps(verdict,indent=2)+'\n')
    print(json.dumps({'status':verdict['status'],'execution':stats,'same98':corpus['counts'],'long_case_seconds':actual['witness_nonatomic_100']['seconds']},indent=2))

if __name__=='__main__':
    main()
