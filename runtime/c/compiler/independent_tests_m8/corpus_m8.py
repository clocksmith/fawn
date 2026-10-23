"""Same98 unchanged sources through the actual consumer, without EMIT-SKIP passes."""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
import re
import struct

import corpus_m7
from corpus_m6 import CONSUMER_SHA256, LEDGER_SHA256
from compute_oracle_m8 import Module
from retained_m8 import LegacyAdapter
from semantic_checks import run_vectors
from semantic_m5 import execute_vectors as uniform_vectors
from semantic_m8 import execute_vectors,sha,write_json
import witnesses_m8


def excluded_feature(source: str) -> dict:
    clean=re.sub(r'//[^\n]*|/\*.*?\*/',lambda m:' '*len(m[0]),source,flags=re.S)
    patterns=[('extension directive',r'\benable\s+\w+'),
      ('workgroup variable',r'var\s*<\s*workgroup\s*>'),
      ('vector storage element',r'array\s*<\s*vec\w*'),
      ('noncompute stage',r'@(?:vertex|fragment)\b'),
      ('excluded mathematical builtin',r'\b(?:exp|tanh|sqrt|inverseSqrt|sin|cos|pow|fract|clamp|dot|select|arrayLength|min)\s*\('),
      ('texture or sampler resource',r'\b(?:texture_\w+|sampler(?:_comparison)?)\b')]
    for reason,pattern in patterns:
        match=re.search(pattern,clean)
        if match:return dict(reason=reason,byte_offset=len(source[:match.start()].encode()),source_fragment=match[0])
    for reason,pattern,maximum in [('multiple functions/helpers',r'\bfn\s+\w+',1),
                                  ('more than eight resources',r'var\s*<\s*(?:storage|uniform)\b',8)]:
        matches=list(re.finditer(pattern,clean))
        if len(matches)>maximum:
            m=matches[maximum]
            return dict(reason=reason,count=len(matches),byte_offset=len(source[:m.start()].encode()),source_fragment=m[0])
    # Builtin parameter vec3<u32> is retained. Actual constructors/general values are not.
    match=re.search(r'\bvec[234](?:\s*<[^>]+>|[fiuh])?\s*\(',clean)
    if match:return dict(reason='general vector value',byte_offset=len(source[:match.start()].encode()),source_fragment=match[0])
    raise ValueError('Original source requires explicit out-of-contract feature review')


def frozen_inventory(repo: Path) -> tuple:
    rows,previous=corpus_m7.frozen_inventory(repo)
    classes={}
    for row in rows:
        name=row['source'];source=(repo/name).read_bytes()
        if previous[name]['capability']!='still_excluded':classes[name]=previous[name]
        elif name in witnesses_m8.WITNESSES:
            if sha(source)!=witnesses_m8.WITNESSES[name]:raise ValueError('M8 witness changed '+name)
            classes[name]=dict(reason='Frozen unchanged M8 numeric witness',capability='M8')
        else:
            try:classes[name]=dict(excluded_feature(source.decode()),capability='still_excluded')
            except ValueError as e:raise ValueError(name+': '+str(e)) from e
    return rows,classes


def instruction_cases(repo: Path) -> dict:
    cases=corpus_m7.instruction_cases(repo)
    for name,source,expected in witnesses_m8.positives(repo):cases[sha(source)]=('compute_m8',name,expected)
    return cases


def execute_output(raw: bytes,case: tuple) -> dict:
    kind,name,expected=case;words=list(struct.unpack('<'+'I'*(len(raw)//4),raw))
    if kind in {'storage','uniform'}:
        module=LegacyAdapter(words);structure=module.interface(expected)
        proof=dict(cpu_invocations=(run_vectors if kind=='storage' else uniform_vectors)(module,expected))
    else:
        module=Module(words);structure=module.interface(expected);proof=execute_vectors(module,expected)
    return dict(fixture=name,structure=structure,**proof)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['repo','cli','validator','source-root','out']:
        parser.add_argument('--'+name,type=Path,required=name in {'repo','out'})
    parser.add_argument('--freeze',action='store_true',help='Read only unchanged source and prior suite, never candidate.')
    args=parser.parse_args();args.repo=args.repo.resolve();args.out.mkdir(parents=True,exist_ok=False)
    previous,classes=frozen_inventory(args.repo);known=instruction_cases(args.repo)
    inventory=[dict(source=r['source'],source_sha256=r['source_sha256'],
                    expected_admitted=classes[r['source']]['capability']!='still_excluded',
                    classification=classes[r['source']],
                    semantic_fixture=known.get(r['source_sha256'],[None,None])[1]) for r in previous]
    write_json(args.out/'inventory.json',inventory)
    if args.freeze:
        print(json.dumps(dict(paths=len(inventory),expected_admitted=sum(x['expected_admitted'] for x in inventory),
                             inventory_sha256=sha((args.out/'inventory.json').read_bytes()))));return 0
    if any(getattr(args,k) is None for k in ['cli','validator','source_root']):parser.error('run needs cli, validator, source-root')
    cli=args.cli.resolve();gate=corpus_m7.load_consumer(args.repo/'bench/gates/spirv_val_gate.py')
    owners={p.name:p.read_text() for p in (args.source_root/'src').glob('*.c')}
    prebuilt=[]
    for path in gate.collect_spv_files(gate.DEFAULT_SPV_SEARCH_DIRS):
        result=corpus_m7.validation(path,args.validator)
        prebuilt.append(dict(path=str(path),sha256=sha(path.read_bytes()),validation=result,
                             passed=result['returncode']==0,candidate_acceptance=False))
    write_json(args.out/'prebuilt-observations.json',prebuilt);records=[]
    for index,(old,frozen) in enumerate(zip(previous,inventory)):
        name=old['source'];path=args.repo/name;source=path.read_bytes();directory=args.out/f'source_{index:03d}';directory.mkdir();output=directory/'module.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(cli),path,output);errors=[];target=proof=diagnostic=phase=diagnostic_owner=None;raw=None
        if bool(ok)!=frozen['expected_admitted']:errors.append('Frozen source admission differs')
        if path.read_bytes()!=source:errors.append('Original source changed')
        if {p.name for p in directory.iterdir()}!=({'module.spv'} if ok else set()):errors.append('Unexpected sidecar/output residue')
        stages={k:'passed' for k in ('lexing','parsing','semantic_validation','emission')}
        if not ok:
            try:stages,diagnostic,phase,diagnostic_owner=corpus_m7.rejected_stages(source,message,owners)
            except ValueError as e:errors.append(str(e));stages={k:'not_established' for k in stages}
        else:
            raw=output.read_bytes();target=corpus_m7.validation(output,args.validator)
            if target['returncode']:errors.append('Explicit Vulkan1.1 target validation failed')
            if sha(source) not in known:errors.append('No frozen independent instruction fixture for emitted source')
            else:
                try:proof=execute_output(raw,known[sha(source)])
                except (ValueError,RuntimeError,AssertionError,KeyError,IndexError,TypeError) as e:errors.append(type(e).__name__+': '+str(e))
        stages.update(external_vulkan11_validation='passed' if target and not target['returncode'] else 'failed' if target else 'not_run_no_module',
                      independent_instruction_execution='passed' if proof else 'failed' if ok else 'not_run_no_module',
                      backend_qualification='not_run_no_physical_backend',runtime_adoption='not_run')
        row=dict(frozen,admitted=ok,emission_skipped=not ok,real_consumer_message=message,diagnostic=diagnostic,
                 first_rejection_stage=phase,diagnostic_owner=diagnostic_owner,output_sha256=sha(raw) if raw is not None else None,
                 consumer_command=[str(cli),'--shader-path',str(path),'--out',str(output)],validation=target,execution=proof,
                 stages=stages,errors=errors,passed=not errors)
        records.append(row);write_json(args.out/'records.json',records)
        if errors:print(json.dumps(row,sort_keys=True),flush=True)
    admitted=[r for r in records if r['admitted']]
    counts=dict(paths=len(records),unique_sources=91,attempted_sources=len(records),emitted=len(admitted),
                emission_skipped=len(records)-len(admitted),skipped_not_acceptance_passes=len(records)-len(admitted),
                unique_admitted=len({r['source_sha256'] for r in admitted}),
                emitted_target_validated=sum(bool(r['validation'] and not r['validation']['returncode']) for r in records),
                instruction_executed=sum(r['stages']['independent_instruction_execution']=='passed' for r in records),
                cpu_invocations=sum(r['execution']['cpu_invocations'] for r in records if r['execution']),
                prebuilt_observed=len(prebuilt),prebuilt_target_validated=sum(r['passed'] for r in prebuilt),
                first_rejection_stages=dict(collections.Counter(r['first_rejection_stage'] for r in records if r['first_rejection_stage'])))
    report=dict(kind='M8 same98 unchanged real consumer',counts=counts,records=records,prebuilt=prebuilt,
                candidate_cli_sha256=sha(cli.read_bytes()),consumer_sha256=CONSUMER_SHA256,frozen_ledger_sha256=LEDGER_SHA256,
                validator_sha256=sha(args.validator.read_bytes()),phase_mapping_source_hashes={n:sha(t.encode()) for n,t in owners.items()},
                passed=all(r['passed'] for r in records),failed=[r for r in records if not r['passed']],
                notes=['Prebuilt modules are observations, never candidate acceptance.','EMIT-SKIP is never admission or validation.',
                       'No physical backend, runtime adoption, performance or whole-migration claim.'])
    write_json(args.out/'results.json',report);print(json.dumps(counts,indent=2));return int(not report['passed'])


if __name__=='__main__':raise SystemExit(main())
