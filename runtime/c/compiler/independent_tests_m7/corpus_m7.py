"""Same98 immutable source ledger through the unchanged real emitter consumer.

Prebuilt modules, attempted/emitted/skipped source and target validation stay
separate. Gate-main's EMIT-SKIP-as-pass behavior is never an acceptance result.
"""
from __future__ import annotations

import argparse
import collections
import importlib.util
import json
from pathlib import Path
import re
import subprocess

import all_fixtures
import fixtures_m5
import fixtures_m6
from compute_oracle_m6 import Module as ComputeModuleM6
from compute_oracle_m7 import Module as ComputeModuleM7
from corpus_m6 import CONSUMER_SHA256, LEDGER_SHA256, WITNESSES as M6_WITNESSES
from corpus_m6 import diagnostic_phase
from semantic_checks import run_vectors
from spirv_oracle_base import ExecutionError, OracleLimit, StructuralError
from semantic_m5 import execute_vectors as execute_uniform
from semantic_m6 import execute_vectors as execute_compute
from typed_oracle import Module as StorageModule
from uniform_oracle_m5 import Module as UniformModule
from witnesses_m7 import ADDITIONAL, WITNESSES, execute_vectors
from witnesses_m7 import positives, sha, unpacked, write_json


def excluded_feature(source: str) -> dict:
    """Source-only excluded feature, not a predicted compiler failure phase."""
    clean = re.sub(r'//[^\n]*|/\*.*?\*/', lambda m: ' ' * len(m[0]), source,
                   flags=re.S)
    patterns = [
        ('extension directive', r'\benable\s+\w+'),
        ('workgroup variable', r'var\s*<\s*workgroup\s*>'),
        ('vector storage element', r'array\s*<\s*vec\w*'),
        ('bitwise operations', r'\^|>>|<<|(?<!&)&(?!&)|(?<!\|)\|(?!\|)'),
        ('division or remainder', r'/(?!=|[/*])|%'),
        ('noncompute stage', r'@(?:vertex|fragment)\b'),
        ('excluded cast or mathematical builtin',
         r'\b(?:f32|u32|i32|exp|tanh|sqrt|inverseSqrt|sin|cos|pow|fract|clamp|dot|select|arrayLength)\s*\('),
        ('general vector values', r'vec[234]\s*<'),
    ]
    for reason, pattern in patterns:
        match = re.search(pattern, clean)
        if match:
            return dict(reason=reason, byte_offset=len(source[:match.start()].encode('utf-8')),
                        source_fragment=match[0])
    for reason, pattern, maximum in [
        ('multiple functions/helpers', r'\bfn\s+\w+', 1),
        ('multiple module constants', r'\bconst\s+\w+', 1),
        ('more than eight resources', r'var\s*<\s*(?:storage|uniform)\b', 8),
    ]:
        matches = list(re.finditer(pattern, clean))
        if len(matches) > maximum:
            found = matches[maximum]
            return dict(reason=reason, count=len(matches),
                        byte_offset=len(source[:found.start()].encode('utf-8')),
                        source_fragment=found[0])
    raise ValueError('Frozen rejection needs explicit source-only feature review')


def frozen_inventory(repo: Path) -> tuple[list, dict]:
    ledger = repo / 'docs/c-migration/corpus-coverage-m5.json'
    consumer = repo / 'bench/gates/spirv_val_gate.py'
    if sha(ledger.read_bytes()) != LEDGER_SHA256:
        raise ValueError('Original source ledger hash differs')
    if sha(consumer.read_bytes()) != CONSUMER_SHA256:
        raise ValueError('Unchanged emitter consumer hash differs')
    rows = json.loads(ledger.read_text(encoding='utf-8'))['records']
    if len(rows) != 98 or len({row['source_sha256'] for row in rows}) != 91:
        raise ValueError('Frozen ledger is not 98 paths / 91 unique sources')
    selected = M6_WITNESSES | WITNESSES | ADDITIONAL
    classifications = {}
    for row in rows:
        source = (repo / row['source']).read_bytes()
        if sha(source) != row['source_sha256']:
            raise ValueError('Original source changed: ' + row['source'])
        if row['source'] in selected and sha(source) != selected[row['source']]:
            raise ValueError('Selected witness hash differs: ' + row['source'])
        if row['admitted']:
            classification = dict(reason='Retained M5 source admission', capability='M5')
        elif row['source'] in M6_WITNESSES:
            classification = dict(reason='Retained exact M6 compute witness', capability='M6')
        elif row['source'] in WITNESSES:
            classification = dict(reason='Frozen M7 unchanged loop/state witness',
                                  capability='M7 job and binary/reflection-v2 CLI')
        elif row['source'] in ADDITIONAL:
            classification = dict(reason='Else-if plus direct contextual unsuffixed storage index',
                                  capability='M7 job and binary/reflection-v2 CLI')
        else:
            classification = excluded_feature(source.decode('utf-8'))
            classification['capability'] = 'still_excluded'
        classifications[row['source']] = classification
    expected = sum(row['admitted'] or row['source'] in selected for row in rows)
    if expected != 17:
        raise ValueError('Source-only M7 admission inventory differs from frozen 17')
    return rows, classifications


def load_consumer(path: Path):
    spec = importlib.util.spec_from_file_location('unchanged_real_emitter_m7', path)
    if spec is None or spec.loader is None:
        raise ImportError(str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validation(path: Path, validator: Path) -> dict:
    command = [str(validator), '--target-env', 'vulkan1.1', str(path)]
    result = subprocess.run(command, capture_output=True, text=True,
                            timeout=30, check=False)
    return dict(command=command, returncode=result.returncode,
                stdout=result.stdout, stderr=result.stderr)


def instruction_cases(repo: Path) -> dict:
    known = {}
    for kind, cases in [('storage', all_fixtures.admitted(repo)),
                         ('uniform', fixtures_m5.admitted(repo)),
                         ('compute_m6', fixtures_m6.positives(repo)),
                         ('compute_m7', positives(repo))]:
        for name, source, expected in cases:
            known[sha(source)] = (kind, name, expected)
    return known


def execute_output(raw: bytes, case: tuple) -> dict:
    kind, name, expected = case
    types = {'storage': StorageModule, 'uniform': UniformModule,
             'compute_m6': ComputeModuleM6, 'compute_m7': ComputeModuleM7}
    module = types[kind](unpacked(raw))
    structure = module.interface(expected)
    if kind == 'compute_m7':
        proof = execute_vectors(module, expected)
    elif kind == 'compute_m6':
        count, synthetic, dispatches = execute_compute(module, expected)
        proof = dict(cpu_invocations=count, synthetic_single_invocations=synthetic,
                     coherent_dispatch_trials=dispatches)
    else:
        count = (execute_uniform if kind == 'uniform' else run_vectors)(module, expected)
        proof = dict(cpu_invocations=count)
    return dict(fixture=name, structure=structure, **proof)


def rejected_stages(source: bytes, message: str, owners: dict) -> tuple:
    stages = {key: 'not_established' for key in
              ('lexing', 'parsing', 'semantic_validation', 'emission')}
    match = re.search(r'category=(\d+) offset=(\d+): (.*)', message)
    if match is None:
        raise ValueError('Actual consumer rejection has no compiler diagnostic')
    offset = int(match[2])
    if offset > len(source):
        raise ValueError('Diagnostic is outside original source')
    prefix = source[:offset]
    diagnostic = dict(status=int(match[1]), offset=offset, line=prefix.count(b'\n') + 1,
                      column=len(prefix.rsplit(b'\n', 1)[-1]) + 1, message=match[3])
    phase, owner = diagnostic_phase(match[3], owners)
    reached = True
    for stage in stages:
        if not reached:
            stages[stage] = 'not_reached'
        elif stage == phase:
            stages[stage] = 'rejected'
            reached = False
        else:
            stages[stage] = 'passed'
    return stages, diagnostic, phase, owner


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['repo', 'build', 'validator', 'out', 'source-root']:
        parser.add_argument('--' + name, type=Path, required=name == 'repo',
                            help='Explicit ' + name + ' path.')
    parser.add_argument('--plan', action='store_true', help='Freeze source inventory only, no candidate use.')
    args = parser.parse_args()
    args.repo = args.repo.resolve()
    previous, classifications = frozen_inventory(args.repo)
    if args.plan:
        print(json.dumps(dict(paths=98, unique_sources=91, expected_admitted=17,
                              expected_emission_skipped=81, classifications=classifications),
                         indent=2, sort_keys=True))
        return 0
    if any(getattr(args, key) is None for key in ['build', 'validator', 'out']):
        parser.error('execution requires --build, --validator and --out')
    cli = args.build.resolve() / 'doe-compiler-c'
    args.out.mkdir(parents=True, exist_ok=False)
    consumer = args.repo / 'bench/gates/spirv_val_gate.py'
    gate = load_consumer(consumer)
    owner = (args.source_root.resolve() if args.source_root else
             args.repo / 'runtime/c/compiler') / 'src'
    owners = {path.name: path.read_text(encoding='utf-8') for path in owner.glob('*.c')}
    known = instruction_cases(args.repo)
    selected = M6_WITNESSES | WITNESSES | ADDITIONAL
    prebuilt = []
    for path in gate.collect_spv_files(gate.DEFAULT_SPV_SEARCH_DIRS):
        # These are observational baseline artifacts, never current C emissions.
        result = validation(path, args.validator)
        prebuilt.append(dict(path=str(path.relative_to(args.repo)), sha256=sha(path.read_bytes()),
                             validation=result, passed=result['returncode'] == 0,
                             candidate_acceptance=False))
    write_json(args.out / 'prebuilt-observations.json', prebuilt)
    records = []
    for index, old in enumerate(previous):
        name = old['source']
        path = args.repo / name
        source = path.read_bytes()
        expected = old['admitted'] or name in selected
        directory = args.out / f'source_{index:03d}'
        directory.mkdir()
        output = directory / 'module.spv'
        ok, message = gate.emit_spirv_for_wgsl(str(cli), path, output)
        errors = []
        if bool(ok) != bool(expected):
            errors.append(f'Expected admission={expected}; observed={ok}')
        if path.read_bytes() != source:
            errors.append('Original WGSL changed')
        if {p.name for p in directory.iterdir()} != ({'module.spv'} if ok else set()):
            errors.append('Unexpected sidecar, residue or missing output')
        stages = {key: 'passed' for key in ('lexing', 'parsing', 'semantic_validation', 'emission')}
        target = diagnostic = phase = diagnostic_owner = proof = None
        raw = None
        if not ok:
            try:
                stages, diagnostic, phase, diagnostic_owner = rejected_stages(source, message, owners)
            except ValueError as exc:
                errors.append(str(exc))
                stages = {key: 'not_established' for key in stages}
        else:
            raw = output.read_bytes()
            target = validation(output, args.validator)
            if target['returncode']:
                errors.append('Explicit Vulkan1.1 target validation failed')
            if sha(source) not in known:
                errors.append('No frozen independent opcode fixture for emitted source')
            else:
                try:
                    proof = execute_output(raw, known[sha(source)])
                except (ExecutionError, OracleLimit, StructuralError, ValueError,
                        RuntimeError, AssertionError, KeyError, IndexError, TypeError) as exc:
                    errors.append('Actual consumer-output execution: ' + type(exc).__name__ + ': ' + str(exc))
        stages.update(external_vulkan11_validation=(
            'passed' if target and not target['returncode'] else 'failed' if target else 'not_run_no_module'),
            independent_instruction_execution='passed' if proof else 'failed' if ok else 'not_run_no_module',
            backend_qualification='not_run_no_physical_backend', runtime_adoption='not_run')
        row = dict(source=name, source_sha256=sha(source), source_bytes=len(source),
                   expected_admitted=bool(expected), admitted=ok, emission_skipped=not ok,
                   first_rejection_stage=phase, diagnostic_owner=diagnostic_owner, diagnostic=diagnostic,
                   real_consumer_message=message,
                   consumer_command=[str(cli), '--shader-path', str(path), '--out', str(output)],
                   output=str(output) if ok else None, output_sha256=sha(raw) if raw is not None else None,
                   validation=target, execution=proof, stages=stages,
                   independent_execution_trials=proof['cpu_invocations'] if proof else 0,
                   static_feature_inspection=classifications[name],
                   m5_first_rejection_stage=old['first_rejection_stage'],
                   errors=errors, passed=not errors)
        records.append(row)
        write_json(args.out / 'records.json', records)
        if errors:
            print(json.dumps(row, sort_keys=True), flush=True)
    admitted = [row for row in records if row['admitted']]
    counts = dict(paths=len(records), unique_sources=91, attempted_sources=len(records),
                  emitted=len(admitted), admitted=len(admitted), emission_skipped=len(records)-len(admitted),
                  skipped_not_acceptance_passes=len(records)-len(admitted),
                  unique_admitted=len({r['source_sha256'] for r in admitted}),
                  emitted_target_validated=sum(bool(r['validation'] and not r['validation']['returncode']) for r in records),
                  instruction_executed=sum(r['stages']['independent_instruction_execution'] == 'passed' for r in records),
                  prebuilt_observed=len(prebuilt), prebuilt_target_validated=sum(r['passed'] for r in prebuilt),
                  prebuilt_not_candidate_acceptance=len(prebuilt),
                  first_rejection_stages=dict(collections.Counter(r['first_rejection_stage']
                                                                 for r in records if r['first_rejection_stage'])))
    report = dict(kind='M7 same98 unchanged real emitter, source-only frozen capabilities',
                  candidate_cli_sha256=sha(cli.read_bytes()), consumer_sha256=CONSUMER_SHA256,
                  frozen_ledger_sha256=LEDGER_SHA256, validator_sha256=sha(args.validator.read_bytes()),
                  phase_mapping_source_hashes={name: sha(text.encode('utf-8')) for name, text in owners.items()},
                  classification_basis='Actual diagnostic source-bound to owning phase; excluded feature inventory is separately source-only.',
                  counts=counts, records=records, prebuilt=prebuilt,
                  passed=all(r['passed'] for r in records), failed=[r for r in records if not r['passed']],
                  notes=['Prebuilt observations are not candidate outputs or compiler acceptance.',
                         'Rejected source is never promoted by EMIT-SKIP.',
                         'No physical backend, runtime adoption, performance or whole-migration claim.'])
    write_json(args.out / 'corpus-coverage-m7.json', report)
    write_json(args.out / 'results.json', report)
    print(json.dumps(counts, indent=2, sort_keys=True), flush=True)
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
