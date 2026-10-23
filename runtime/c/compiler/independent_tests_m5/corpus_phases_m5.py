"""Bind observed corpus diagnostics to compiler phases, separate from static feature inspection."""
from __future__ import annotations
import argparse
import collections
import hashlib
import json
from pathlib import Path

LEX_DIAGNOSTIC='unsupported character or operator in bounded WGSL grammar'
PARSE_DIAGNOSTICS={
    'expected at least one resource declaration; unsupported or malformed WGSL subset',
    'expected one @compute and one @workgroup_size(N); unsupported or malformed WGSL subset',
    'expected global_invocation_id; unsupported or malformed WGSL subset',
    'expected ); unsupported or malformed WGSL subset',
    'resource declaration count exceeds 8 bindings',
    'expected ]; unsupported or malformed WGSL subset',
}

def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','source-root','corpus','execution','output']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();owner=a.source_root
    result=json.loads(a.corpus.read_text());execution=json.loads(a.execution.read_text())
    executed={r['source']:r for r in execution['records']}
    old={r['source']:r for r in json.loads((a.repo/'docs/c-migration/corpus-coverage-m4.json').read_text())['records']}
    source_hashes={name:sha(owner/'src'/name) for name in ['compile.c','lex.c','parse.c','validate.c','spirv.c']}
    records=[]
    for row in result['records']:
        source=(a.repo/row['source']).read_bytes()
        if hashlib.sha256(source).hexdigest()!=row['source_sha256']:raise ValueError('Source changed '+row['source'])
        phase=None;diagnostic_owner=None;diagnostic=None
        stages={k:'passed' for k in ['lexing','parsing','semantic_validation','emission']}
        if not row['admitted']:
            d=row['first_diagnostic'];message=d['message']
            if message==LEX_DIAGNOSTIC:phase='lexing';diagnostic_owner='src/lex.c'
            elif message in PARSE_DIAGNOSTICS:phase='parsing';diagnostic_owner='src/parse.c'
            else:raise ValueError('Unmapped observed diagnostic: '+message)
            offset=d['byte_offset'];prefix=source[:offset]
            diagnostic=dict(status=d['category'],offset=offset,line=prefix.count(b'\n')+1,column=len(prefix.rsplit(b'\n',1)[-1])+1,message=message)
            reached=True
            for name in stages:
                if not reached:stages[name]='not_reached'
                elif name==phase:stages[name]='rejected';reached=False
        stages['external_vulkan11_validation']=row['stages']['external_vulkan11_validation']
        witness=executed.get(row['source'])
        if witness:
            if witness['source_sha256']!=row['source_sha256'] or witness['output_sha256']!=row['output_sha256'] or not witness['passed']:raise ValueError('Execution source/artifact binding mismatch')
            stages['independent_instruction_execution']='passed'
        else:stages['independent_instruction_execution']='not_run_no_module' if not row['admitted'] else 'not_run'
        stages['backend_qualification']='not_run_no_physical_backend'
        records.append(dict(source=row['source'],source_sha256=row['source_sha256'],source_bytes=row['source_bytes'],admitted=row['admitted'],first_rejection_stage=phase,diagnostic_owner=diagnostic_owner,diagnostic=diagnostic,original_source_at_diagnostic=row['first_diagnostic']['original_source_at_diagnostic'] if row['first_diagnostic'] else None,real_consumer_message=row['real_consumer_message'],consumer_command=row['consumer_command'],output_sha256=row['output_sha256'],stages=stages,m4_first_rejection_stage=old[row['source']]['first_rejection_stage'],independent_execution_trials=witness['cpu_trials'] if witness else 0,static_feature_inspection=row['classification']))
    counts=dict(result['counts'],first_rejection_stages=dict(collections.Counter(r['first_rejection_stage'] for r in records if r['first_rejection_stage'])),instruction_executed=sum(r['stages']['independent_instruction_execution']=='passed' for r in records))
    report=dict(kind='M5 source-bound phase-separated unchanged-consumer audit',candidate_cli_sha256=result['cli_sha256'],consumer_sha256=result['consumer_sha256'],validator_sha256=result['validator_sha256'],phase_mapping_source_hashes=source_hashes,classification_basis='Exact observed diagnostic messages are emitted uniquely by lex.c or parse.c. compile.c short-circuits lex, parse, validate, emit in that order. Successful emitted module establishes those phases; external validation and source-bound actual-opcode execution are separately recorded. Static missing-feature inspection is not the observed failure phase.',counts=counts,records=records,notes=['No gate-main execution or EMIT-SKIP-as-pass.','No physical backend, runtime adoption, GPU performance or full migration claim.'])
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(counts,indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
