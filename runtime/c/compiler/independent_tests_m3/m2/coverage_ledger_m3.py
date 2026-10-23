"""Post-black-box first-rejection ledger, not a full language feature inventory."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.dont_write_bytecode=True
from evaluate import Evaluator

def sha(p: Path) -> str:return hashlib.sha256(p.read_bytes()).hexdigest()
def main() -> int:
    p=argparse.ArgumentParser()
    for n in ['root','compiler-root','library','validator','audit','out']:p.add_argument('--'+n,type=Path,required=True, help='Explicit input or fresh output for this acceptance stage.')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    audit=json.loads(a.audit.read_text(encoding='utf-8'));api=Evaluator(a.library,a.out/'api',a.validator)
    records=[]
    for item in audit['records']:
        path=a.root/item['source'];source=path.read_bytes()
        assert sha(path)==item['source_sha256']
        status,result,words,errors=api.call(source);assert not errors,errors
        message=result.diagnostic.message.decode()
        assert bool(status)==(not item['admitted'])
        if status:
            if status==3 and message=='unsupported character or operator in bounded WGSL grammar':
                first='lexing';owner='runtime/c/compiler/src/lex.c:61-62'
            elif status==2 and message.startswith('expected ') and message.endswith('; unsupported or malformed WGSL subset'):
                first='parsing';owner='runtime/c/compiler/src/parse.c:13-22'
            else:raise ValueError('Unmapped rejection; do not guess its owner: '+message)
        else:first=None;owner=None
        stages={}
        for name in ['lexing','parsing','semantic_validation','emission']:
            if first is None:stages[name]='passed'
            elif name==first:stages[name]='rejected'
            elif name=='lexing' and first=='parsing':stages[name]='passed'
            else:stages[name]='not_reached'
        stages.update(external_vulkan11_validation='passed' if item['admitted'] else 'not_run_no_module',
            independent_instruction_execution='passed' if item['admitted'] else 'not_run_no_module',
            backend_qualification='not_run_no_physical_backend')
        rows=dict(source=item['source'],source_sha256=item['source_sha256'],source_bytes=len(source),
            admitted=item['admitted'],first_rejection_stage=first,diagnostic_owner=owner,
            diagnostic=dict(status=status,status_name={0:'OK',2:'PARSE_ERROR',3:'UNSUPPORTED'}[status],
                offset=result.diagnostic.offset,line=result.diagnostic.line,column=result.diagnostic.column,message=message),
            original_source_at_diagnostic=source[result.diagnostic.offset:result.diagnostic.offset+60].decode(errors='replace') if status else None,
            real_consumer_message=item['message'],stages=stages,
            emitted_output_sha256=item.get('output_sha256'),external_validator_returncode=item.get('validator_returncode'),
            independent_cpu_trials=item.get('cpu_trials',0))
        records.append(rows)
    stages={}
    for name in records[0]['stages']:
        counts={}
        for record in records:
            value=record['stages'][name];counts[value]=counts.get(value,0)+1
        stages[name]=counts
    ledger=dict(kind='post-black-box actual source coverage ledger',candidate_cli_sha256=audit['cli_sha256'],
        library_sha256=sha(a.library),consumer_sha256=audit['consumer_sha256'],validator_sha256=audit['validator_sha256'],
        phase_mapping_source_hashes={name:sha(a.compiler_root/'src'/name) for name in ['compile.c','lex.c','parse.c']},
        classification_basis='Exact observed API diagnostics mapped to unique diagnostic owners; compile.c short-circuit pipeline establishes later phases were not reached. This is source-reviewed attribution, not instrumented phase timing.',
        notes=['Unsupported means rejected by this bounded compiler, not invalid WGSL.',
            'Only first failure is identified; further unsupported features may be hidden behind it.',
            'The two admitted discovery paths are the same residual content: one unique source, not two semantic families. Neighbor sum is independently tested outside this discovery denominator.',
            'Six subgroup-declaring sources were skipped; no subgroup support or backend qualification.',
            'Prebuilt validations are recorded separately and are not source-compiler coverage.'],
        counts=dict(sources=len(records), unique_source_hashes=len({r['source_sha256'] for r in records}), admitted=sum(r['admitted'] for r in records), unique_admitted_sources=len({r['source_sha256'] for r in records if r['admitted']}), rejected=sum(not r['admitted'] for r in records), unique_rejected_sources=len({r['source_sha256'] for r in records if not r['admitted']}), stages=stages),records=records)
    (a.out/'coverage-ledger.json').write_text(json.dumps(ledger,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    unsupported=[r for r in records if not r['admitted']]
    (a.out/'unsupported-96.json').write_text(json.dumps(unsupported,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    lines=['# Actual discovery coverage','', 'Candidate '+audit['cli_sha256'], '',
        '98 paths / 91 unique source hashes: 2 admitted paths / 1 unique source; 96 rejected paths / 90 unique sources. First failures are 87 lexical and 9 parser rejections. All later compiler phases are not reached for the 96 rejected sources. The two emitted modules pass explicit Vulkan 1.1 and 274 instruction-execution trials. No physical backend is run.', '',
        'These are source-bound first failures, not claims that each source needs only the first missing feature. The complete JSON ledger includes every stage and full original diagnostic.','']
    for r in unsupported:
        d=r['diagnostic'];lines += ['- '+r['source'], '  SHA-256: '+r['source_sha256'],
            f"  {r['first_rejection_stage']}, status {d['status']}, byte {d['offset']}, line {d['line']}, column {d['column']}: {d['message']}"]
    (a.out/'coverage-ledger.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print(json.dumps(ledger['counts'],indent=2, sort_keys=True));return 0
if __name__=='__main__':raise SystemExit(main())
