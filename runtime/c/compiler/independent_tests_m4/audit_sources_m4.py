"""Single 98-path fresh-output audit using the unchanged consumer, never gate main."""
from __future__ import annotations
import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import all_fixtures
from evaluate import Evaluator
from semantic_checks import run_vectors
from typed_oracle import Module


def sha(path: Path) -> str:return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['root','compiler-root','library','cli','validator','out']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    previous=json.loads((a.root/'docs/c-migration/corpus-coverage-m3.json').read_text())
    assert len(previous['records'])==98
    spec=importlib.util.spec_from_file_location('unchanged_gate',a.root/'bench/gates/spirv_val_gate.py');gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
    assert sha(a.root/'bench/gates/spirv_val_gate.py')=='f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    assert {str(path.relative_to(a.root)) for path in gate.discover_wgsl_files(gate.DEFAULT_WGSL_DISCOVERY_DIRS)}=={r['source'] for r in previous['records']}
    expected={source:ex for _,source,ex in all_fixtures.admitted(a.root)};api=Evaluator(a.library,a.out/'api',a.validator);records=[]
    for i,prior in enumerate(previous['records']):
        source=a.root/prior['source'];raw=source.read_bytes();assert sha(source)==prior['source_sha256'];output=a.out/f'{i:03}_{source.stem}.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(a.cli),source,output)
        status,result,words,errors=api.call(raw);assert not errors,errors;assert ok==(status==0)
        diagnostic=result.diagnostic.message.decode();first=None;owner=None
        if status:
            if diagnostic=='unsupported character or operator in bounded WGSL grammar':first='lexing';owner='src/lex.c'
            elif diagnostic.startswith('expected ') or diagnostic in ['decimal WGSL integers may not have leading zeroes'] or diagnostic.startswith('only scalar max calls'):
                first='parsing';owner='src/parse.c'
            elif diagnostic.startswith('decimal float requires'):
                first='parsing';owner='src/numeric.c via parse.c'
            else:raise AssertionError('Unmapped exact source-stage diagnostic: '+diagnostic)
        stages={};ordered=['lexing','parsing','semantic_validation','emission']
        for stage in ordered:
            stages[stage]='passed' if first is None or ordered.index(stage)<ordered.index(first) else 'rejected' if stage==first else 'not_reached'
        row=dict(source=prior['source'],source_sha256=sha(source),source_bytes=len(raw),admitted=ok,first_rejection_stage=first,diagnostic_owner=owner,
            diagnostic=dict(status=status,offset=result.diagnostic.offset,line=result.diagnostic.line,column=result.diagnostic.column,message=diagnostic),
            original_source_at_diagnostic=raw[result.diagnostic.offset:result.diagnostic.offset+90].decode(errors='replace') if status else None,
            real_consumer_message=message,stages=stages,m3_first_rejection_stage=prior['first_rejection_stage'])
        if ok:
            assert output.read_bytes()==struct.pack('<'+'I'*len(words),*words)
            run=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(output)],capture_output=True,text=True)
            assert run.returncode==0,run.stderr
            expect=expected[raw];module=Module(words);module.interface(expect);trials=run_vectors(module,expect)
            row.update(emitted_output_sha256=sha(output),external_validator_returncode=run.returncode,independent_cpu_trials=trials,unique_output=output.name)
            stages.update(external_vulkan11_validation='passed',independent_instruction_execution='passed')
        else:
            assert not output.exists();stages.update(external_vulkan11_validation='not_run_no_module',independent_instruction_execution='not_run_no_module')
        stages['backend_qualification']='not_run_no_physical_backend';records.append(row)
    assert all(sha(a.root/r['source'])==r['source_sha256'] for r in records)
    counts=dict(sources=len(records),unique_source_hashes=len({r['source_sha256'] for r in records}),admitted=sum(r['admitted'] for r in records),unique_admitted_sources=len({r['source_sha256'] for r in records if r['admitted']}),rejected=sum(not r['admitted'] for r in records),stages={k:dict(collections.Counter(r['stages'][k] for r in records)) for k in records[0]['stages']})
    ledger=dict(kind='M4 independently source-bound unique-output coverage ledger',candidate_cli_sha256=sha(a.cli),library_sha256=sha(a.library),consumer_sha256=sha(a.root/'bench/gates/spirv_val_gate.py'),validator_sha256=sha(a.validator),phase_mapping_source_hashes={name:sha(a.compiler_root/'src'/name) for name in ['compile.c','lex.c','numeric.c','parse.c','validate.c','spirv.c']},classification_basis='Observed API first diagnostics matched to source-reviewed owners after black-box tests. Short-circuit compile.c determines later stages not reached. No instrumented phase timing and no complete missing-feature census.',counts=counts,records=records,notes=['Same 98 source paths /91 source contents as M3, independently source-hash verified.','Only fresh unique outputs, no prebuilt SPIR-V counted.','No full gate main invocation, no runtime/application adoption and no physical backend/GPU qualification.'])
    (a.out/'coverage-ledger.json').write_text(json.dumps(ledger,indent=2)+'\n');print(json.dumps(counts,indent=2));return 0
if __name__=='__main__':raise SystemExit(main())
