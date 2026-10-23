"""Fresh unchanged exact98 emission/validation with explicitly attributed execution."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
from run_cases import verify

ROOT=Path(__file__).resolve().parent


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo',type=Path,required=True);p.add_argument('--cli',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--witness-results',type=Path);p.add_argument('--old-fresh-results',type=Path)
    a=p.parse_args();verify();a.out.mkdir(parents=True,exist_ok=False)
    inputs=json.loads((ROOT/'SAME98_INPUTS_M12.json').read_text())
    consumer=a.repo/'bench/gates/spirv_val_gate.py'
    assert sha(consumer)==inputs['consumer_sha256']
    spec=importlib.util.spec_from_file_location('unchanged_spirv_val_consumer',consumer);gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
    prior_path=Path(inputs['accepted_m11_receipt']);assert sha(prior_path)==inputs['accepted_m11_receipt_sha256']
    old={r['source']:r for r in json.loads(prior_path.read_text())['records']}
    witnesses={}
    if a.witness_results:
        proof=json.loads(a.witness_results.read_text())
        witnesses={r['source']:r for r in proof['cases']}
    rows=[]
    for index,item in enumerate(inputs['rows']):
        source=a.repo/item['source'];assert sha(source)==item['source_sha256']
        folder=a.out/f'source_{index:03d}';folder.mkdir();target=folder/'module.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(a.cli),source,target)
        row=dict(item,emitted=ok,validated=False,newly_executed=False,carried_forward_execution=False,message=message,errors=[])
        prior=old[item['source']]
        if prior['emitted'] and not ok:row['errors'].append('accepted M11 emission regressed')
        if ok:
            row['target_sha256']=sha(target)
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(target)],capture_output=True,text=True)
            row['validated']=val.returncode==0;row['validator_output']=val.stdout+val.stderr
            if not row['validated']:row['errors'].append('fresh Vulkan1.1 validation failed')
            if prior['executed']:
                if row['target_sha256']==prior['target_sha256']:
                    row['carried_forward_execution']=True
                    row['execution_qualification']='Fresh target is byte-identical to accepted exact-source instruction execution. This is carried-forward behavior, not a newly executed dispatch.'
                    row['prior_execution_receipt_sha256']=inputs['accepted_m11_receipt_sha256']
                else:
                    row['errors'].append('changed retained target needs fresh original-oracle execution')
            elif item['source'] in inputs['mandatory_new_witnesses']:
                proof=witnesses.get(item['source'],{})
                if proof.get('passed') and proof.get('executed') and proof.get('target_sha256')==row['target_sha256']:
                    row['newly_executed']=True;row['fresh_execution_proof']=proof
                else:
                    row['errors'].append('new mandatory witness lacks full exact-target execution proof')
            else:
                row['execution_qualification']='Validation-only emission; no numerical execution qualification inferred.'
        else:
            row['rejected']=True
            if target.exists():row['errors'].append('rejected source left an output target')
            if item['source'] in inputs['mandatory_new_witnesses']:row['errors'].append('mandatory selected whole source remains rejected')
        assert sha(source)==item['source_sha256']
        rows.append(row)
        counts={'paths':len(rows),'emitted':sum(x['emitted'] for x in rows),'validated':sum(x['validated'] for x in rows),'newly_executed':sum(x['newly_executed'] for x in rows),'carried_forward_executed':sum(x['carried_forward_execution'] for x in rows),'execution_qualified':sum(x['newly_executed'] or x['carried_forward_execution'] for x in rows),'validation_only':sum(x['validated'] and not(x['newly_executed'] or x['carried_forward_execution']) for x in rows),'rejected':sum(not x['emitted'] for x in rows)}
        report={'cli_sha256':sha(a.cli),'validator_sha256':sha(a.validator),'counts':counts,'records':rows,'errors':[x['source'] for x in rows if x['errors']],'historical_baseline':{'validated':52,'executed':43,'validation_only':9,'rejected':46},'limitations':['No first-error disappearance counts as full source admission','No validator-only result counts as numerical execution','No physical GPU/runtime/backend or performance qualification']}
        (a.out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
        print(json.dumps({k:row[k] for k in ['source','emitted','validated','newly_executed','carried_forward_execution','errors']},sort_keys=True),flush=True)
    return int(bool(report['errors']))

if __name__=='__main__':raise SystemExit(main())
