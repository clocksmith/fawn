"""Fresh exact98 real-consumer emission; retained and new execution kept distinct."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import corpus_m10_final as accepted

HERE=Path(__file__).resolve().parent
# Apply the same consumer wiring that the accepted corpus main entry installs.
accepted.corpus_m8.Module=accepted.corpus.Module
accepted.corpus_m8.LegacyAdapter=accepted.corpus.LegacyAdapter
accepted.corpus_m8.execute_vectors=accepted.corpus.execute_vectors

def sha(raw):return hashlib.sha256(raw).hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--retained-root',type=Path,required=True);p.add_argument('--cli',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--m11-results',type=Path);a=p.parse_args();a.out.mkdir(exist_ok=False)
    inputs=json.loads((HERE/'SAME98_PRE_CANDIDATE_INPUTS.json').read_text())['rows'];known=accepted.instruction_cases(a.retained_root)
    gate=accepted.corpus_m7.load_consumer(a.retained_root/'bench/gates/spirv_val_gate.py')
    expected_consumer='f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    assert sha((a.retained_root/'bench/gates/spirv_val_gate.py').read_bytes())==expected_consumer
    sealed_cases=json.loads((HERE/'frozen/cases.json').read_text());witnesses={}
    for case in sealed_cases:
        if 'original_source_path'in case:witnesses.setdefault(case['original_source_path'],[]).append(case['id'])
    prior={}
    if a.m11_results:
        prior={row['id']:row for row in json.loads(a.m11_results.read_text())['cases']}
    rows=[]
    for index,source in enumerate(inputs):
        path=a.repo/source['source'];raw=path.read_bytes();assert sha(raw)==source['source_sha256']
        folder=a.out/('source_'+str(index).zfill(3));folder.mkdir();output=folder/'module.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(a.cli),path,output)
        row={**source,'emitted':ok,'message':message,'validated':False,'executed':False,'errors':[]}
        if source['expected_admitted'] and not ok:row['errors'].append('accepted M10 source no longer emits')
        if source['source'] in witnesses and not ok:row['errors'].append('mandatory M11 witness does not emit')
        if ok:
            target=output.read_bytes();row['target_sha256']=sha(target)
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(output)],capture_output=True,text=True)
            row['validated']=val.returncode==0;row['validator_output']=val.stdout+val.stderr
            if not row['validated']:row['errors'].append('default Vulkan1.1 validation failure')
            if source['expected_admitted']:
                try:row['execution']=accepted.execute_output(target,known[source['source_sha256']]);row['executed']=True
                except Exception as error:row['errors'].append('retained independent execution '+repr(error))
            elif source['source'] in witnesses:
                ids=witnesses[source['source']]
                bound=[prior.get(i,{}) for i in ids]
                if all(x.get('outcome')=='passed' and x.get('word_sha256')==row['target_sha256'] and x.get('execution') for x in bound):
                    row['execution']={'reuse_exact_target_proofs':ids,'receipt_path':str(a.m11_results),'receipt_sha256':sha(a.m11_results.read_bytes())};row['executed']=True
                else:row['errors'].append('mandatory CLI target lacks exact-hash independently executed proof')
            else:row['execution_qualification']='new emission validated only; no presealed byte oracle, not instruction-executed acceptance'
        else:
            row['rejected']=True
            if output.exists():row['errors'].append('rejection left output artifact')
        assert path.read_bytes()==raw
        rows.append(row)
        counts={'paths':len(rows),'emitted':sum(x['emitted'] for x in rows),'validated':sum(x['validated'] for x in rows),'executed':sum(x['executed'] for x in rows),'rejected':sum(not x['emitted'] for x in rows)}
        report={'candidate_cli_sha256':sha(a.cli.read_bytes()),'consumer_sha256':expected_consumer,'validator_sha256':sha(a.validator.read_bytes()),'counts':counts,'records':rows,'failed':[x['source'] for x in rows if x['errors']],'notes':['Rejected sources are not passes. Newly validated but unexecuted sources are not independent execution coverage.','No unrelated prebuilt target observations are combined with fresh emissions. No physical GPU/runtime adoption/performance claim.']}
        (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
        print(source['source'],ok,row['validated'],row['executed'],row['errors'],flush=True)
    return bool(report['failed'])
if __name__=='__main__':raise SystemExit(main())
