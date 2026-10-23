"""Fresh retained M11 compilation, exact-target carry-forward or full fresh execution."""
from __future__ import annotations
import argparse
from datetime import datetime,timezone
import hashlib
import importlib.util
import json
from pathlib import Path
import time
from abi_m10 import Compiler
from target_m12 import Module
import workgroup_observed

ROOT=Path(__file__).resolve().parent


def effective_cases(base):
    cases=json.loads((base/'frozen/cases.json').read_text())
    status=json.loads((base/'PRE_CANDIDATE_STATUS_CORRECTION.json').read_text())['changes']
    for case in cases:
        for correction in status:
            if case['id']==correction['id']:
                assert case['source_sha256']==correction['source_sha256'] and case['expected_status']==correction['old_status']
                case['expected_status']=correction['new_status']
    bounds=json.loads((base/'PRE_CANDIDATE_BOUNDS_CORRECTION.json').read_text())
    for case in cases:
        if case['id']==bounds['original_case']:
            assert case['source_sha256']==bounds['original_source_sha256'];case['expected_status']=bounds['corrected_original_status'];case.pop('execution',None)
    cases.append(bounds['new_case'])
    for correction in json.loads((base/'PRE_CANDIDATE_RESERVED_NAME_CORRECTION.json').read_text())['changes']:
        for case in cases:
            if case['id']==correction['id']:
                assert case['source_sha256']==correction['old_source_sha256'];case['source']=correction['new_source_path'];case['source_sha256']=correction['new_source_sha256']
    cases.extend(json.loads((base/'PRE_CANDIDATE_EXECUTION_SUPPLEMENT.json').read_text())['cases'])
    helper=json.loads((base/'PRE_CANDIDATE_HELPER_NAME_CORRECTION.json').read_text())
    for case in cases:
        if case['id']==helper['id']:
            assert case['source_sha256']==helper['old_source_sha256'];case['source']=helper['new_source_path'];case['source_sha256']=helper['new_source_sha256']
    return cases


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repo',type=Path,required=True);p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
    base=a.repo/'runtime/c/compiler/independent_tests_m11';identity=json.loads((ROOT/'RETAINED_M11_BINDING.json').read_text())
    for relative,expected in identity['files'].items():assert hashlib.sha256((base/relative).read_bytes()).hexdigest()==expected,relative
    path=base/'run_cases.py';spec=importlib.util.spec_from_file_location('accepted_m11_runner',path);adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter);adapter.verify()
    # Same frozen output comparisons; the independently materialized instruction
    # model has only M12 numeric support added and scheduler observation is read-only.
    adapter.Module=Module;adapter.Race=workgroup_observed.Race
    active={}
    def observed_dispatch(*args,**kwargs):
        start=time.monotonic()
        def progress(counters):
            row=dict(counters,case=active['id'],elapsed_seconds=time.monotonic()-start,utc=datetime.now(timezone.utc).isoformat())
            with (a.out/'progress.jsonl').open('a') as stream:stream.write(json.dumps(row,sort_keys=True)+'\n')
            print(json.dumps(row,sort_keys=True),flush=True)
        return workgroup_observed.dispatch(*args,**kwargs,progress_callback=progress)
    adapter.dispatch=observed_dispatch
    prior_path=base/'candidate2-observations/SOURCE_BUILT_CASES_FINAL.json';prior={r['id']:r for r in json.loads(prior_path.read_text())['cases']}
    cases=effective_cases(base);assert len(cases)==120;compiler=Compiler(a.library);rows=[]
    for case in cases:
        active['id']=case['id'];start=time.monotonic()
        try:
            row=adapter.evaluate(compiler,case,a.out,a.validator,execute=False)
            if 'execution' in case:
                old=prior[case['id']]
                if old['outcome']=='passed' and old.get('execution') and old['word_sha256']==row['word_sha256']:
                    row['carried_forward_execution']={'source_sha256':case['source_sha256'],'accepted_target_sha256':old['word_sha256'],'receipt_sha256':identity['files']['candidate2-observations/SOURCE_BUILT_CASES_FINAL.json'],'proof':old['execution']}
                    row['newly_executed']=False
                else:
                    row=adapter.evaluate(compiler,case,a.out,a.validator,execute=True)
                    row['newly_executed']=True
            row['outcome']='passed'
        except Exception as error:row={'id':case['id'],'outcome':'failed','reason':repr(error)}
        row['seconds']=time.monotonic()-start;rows.append(row)
        report={'library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),'cases':rows,'failed':[x['id'] for x in rows if x['outcome']=='failed'],'pending':len(cases)-len(rows),'newly_executed_cases':sum(x.get('newly_executed',False) for x in rows),'carried_forward_execution_cases':sum('carried_forward_execution' in x for x in rows),'qualification':'Every source freshly compiled/validated; byte-identical prior targets explicitly carried forward, changed targets require complete original output/schedule/poison execution. No GPU qualification.'}
        (a.out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n');print(case['id'],row['outcome'],row.get('reason',''),flush=True)
    return int(bool(report['failed']))

if __name__=='__main__':raise SystemExit(main())
