"""Same 98 unchanged paths through the actual emitter consumer, phase separated.

This never invokes gate main. Every path has a unique output. Actual SPIR-V
validation, instruction execution, and unrun physical/runtime gates are separate.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess

import all_fixtures
import fixtures_m5
from semantic_checks import run_vectors
from semantic_m5 import execute_vectors
from typed_oracle import Module as StorageModule
from uniform_oracle_m5 import Module as UniformModule

LEDGER_SHA256 = 'e0f6d0277a6136b93fcb8d81cdca0ebf25198d21035c25a8acff74e0ccbe4678'
CONSUMER_SHA256 = 'f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
WITNESSES = {
    'bench/inference-pipeline/kernels/gather.wgsl':
        '062496b48017bd03cf9be3535566c71d6f18b3c86e831ff3c533f8125e7e430f',
    'bench/inference-pipeline/kernels/copy_buffer.wgsl':
        '5522b855dcf090259e0a6e3e9a529c4799295b49be65ef00b453b4dad76acd52',
    'bench/kernels/compilation-corpus/trivial_noop_compute.wgsl':
        '0e88698096320b7a1e81a4ebe37897aebb71c3e6c9cfd085624e287e3c673073',
    'bench/kernels/copy_textures_x32.wgsl':
        '263dbc4ad43346316cfe2c8badad06deb6025b6ebb687e3514b36d26c6883d10',
    'bench/kernels/dispatch_noop.wgsl':
        'd51b69e09c71649b2266dc6f0538ee66a84ed7e433baf613ce71ef68320a8db8',
    'bench/kernels/draw_call_proxy_noop.wgsl':
        'd51b69e09c71649b2266dc6f0538ee66a84ed7e433baf613ce71ef68320a8db8',
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n',
                    encoding='utf-8')


def missing_feature(source: str) -> dict[str, object]:
    """Source-only out-of-contract witness, never a compiler-phase prediction."""
    clean = re.sub(r'//[^\n]*|/\*.*?\*/', lambda m: ' ' * len(m[0]),
                   source, flags=re.S)
    patterns = (
        ('extension directive', r'\benable\s+\w+'),
        ('workgroup variable', r'var\s*<\s*workgroup\s*>'),
        ('loop', r'\b(?:loop|for|while)\s*[(\{]'),
        ('vector storage element', r'array\s*<\s*vec\w*'),
        ('mutable local', r'\bvar\s+\w+'),
        ('bitwise xor', r'\^'),
        ('division', r'/(?!=|[/*])'),
        ('noncompute stage', r'@(?:vertex|fragment)\b'),
        ('logical condition', r'&&|\|\|'),
        ('unsuffixed index', r'\[\s*[0-9]+\s*\]'),
        ('else-if', r'\belse\s+if\b'),
    )
    for label, pattern in patterns:
        match = re.search(pattern, clean)
        if match:
            return dict(reason=label,
                        byte_offset=len(source[:match.start()].encode('utf-8')),
                        source_fragment=match[0])
    functions = list(re.finditer(r'\bfn\s+\w+', clean))
    if len(functions) > 1:
        return dict(reason='multiple functions/helpers', count=len(functions),
                    source_fragment=functions[1][0])
    resources = list(re.finditer(r'var\s*<\s*(?:storage|uniform)\b', clean))
    if len(resources) > 8:
        return dict(reason='more than eight resources', count=len(resources))
    raise ValueError('Frozen rejected source needs explicit M6 feature inspection')


def diagnostic_phase(message: str, owners: dict[str, str]) -> tuple[str, str]:
    """Bind an actual compiler diagnostic to a uniquely owning C source literal.

    Parsing appends a common diagnostic suffix to its detailed token message;
    this remains source-bound evidence, not a feature-based failure estimate.
    Unmapped or ambiguous messages fail instead of guessing the phase.
    """
    matches = []
    stages = {'lex': 'lexing', 'parse': 'parsing', 'validate': 'semantic_validation',
              'spirv': 'emission', 'reflection': 'semantic_validation'}
    for name, text in owners.items():
        stage = next((value for prefix, value in stages.items()
                      if name.startswith(prefix)), None)
        if not stage:
            continue
        literals = re.findall(r'"([^"\n]*)"', text)
        for literal in literals:
            if len(literal) >= 15 and literal in message:
                matches.append((len(literal), stage, name))
    if not matches:
        raise ValueError('Unmapped actual diagnostic: ' + message)
    longest = max(n for n, _, _ in matches)
    owners_found = {(stage, name) for n, stage, name in matches if n == longest}
    if len(owners_found) != 1:
        raise ValueError('Ambiguous actual diagnostic: ' + repr(owners_found))
    return next(iter(owners_found))


def attach_execution(ledger_path: Path, execution: list[dict]) -> dict:
    """Merge actual opcode receipts only after exact source/artifact hash checks."""
    report = json.loads(ledger_path.read_text(encoding='utf-8'))
    by_source = {r['source']: r for r in report['records']}
    for proof in execution:
        row = by_source[proof['source']]
        for key in ('source_sha256', 'output_sha256'):
            if proof[key] != row[key]:
                raise ValueError('Execution binding differs: ' + proof['source'])
        if not row['admitted'] or not proof['passed']:
            raise ValueError('Unsuccessful source/opcode execution cannot be promoted')
        row['stages']['independent_instruction_execution'] = 'passed'
        row['independent_execution_trials'] = proof['cpu_trials']
    report['counts']['instruction_executed'] = sum(
        r['stages']['independent_instruction_execution'] == 'passed'
        for r in report['records'])
    write_json(ledger_path, report)
    return report



def execute_new_witnesses(repo: Path, ledger_path: Path) -> dict:
    """Execute the six already-emitted real consumer outputs, never recompile."""
    from compute_oracle_m6 import Module as ComputeModule
    from fixtures_m6 import positives
    from semantic_m6 import execute_vectors as execute_compute_vectors

    report = json.loads(ledger_path.read_text(encoding='utf-8'))
    by_source = {row['source']: row for row in report['records']}
    cases = {name: (source, expected) for name, source, expected in positives(repo)}
    execution = []
    for source_name, digest in WITNESSES.items():
        row = by_source[source_name]
        case_name = 'real_' + Path(source_name).stem
        fixture_source, expected = cases[case_name]
        actual_source = (repo / source_name).read_bytes()
        if sha(actual_source) != digest or sha(fixture_source) != digest:
            raise ValueError('Original/fixture witness source hash differs: ' + source_name)
        if row['source_sha256'] != digest or not row['admitted']:
            raise ValueError('Consumer did not admit the exact source: ' + source_name)
        raw = Path(row['output']).read_bytes()
        if sha(raw) != row['output_sha256']:
            raise ValueError('Actual consumer output changed: ' + source_name)
        words = list(struct.unpack('<' + 'I' * (len(raw) // 4), raw))
        errors = []
        trials = synthetic = dispatches = 0
        try:
            module = ComputeModule(words)
            module.interface(expected)
            trials, synthetic, dispatches = execute_compute_vectors(module, expected)
        except (ValueError, RuntimeError, AssertionError, KeyError, IndexError,
                TypeError) as exc:
            errors.append(type(exc).__name__ + ': ' + str(exc))
        execution.append(dict(source=source_name, source_sha256=digest,
                              output_sha256=sha(raw), fixture=case_name,
                              cpu_trials=trials, synthetic_single_invocations=synthetic,
                              coherent_dispatch_trials=dispatches,
                              passed=not errors, errors=errors,
                              physical_backend='not_run', runtime_adoption='not_run'))
        write_json(ledger_path.parent / 'new-witness-execution.json', execution)
        if errors:
            print(json.dumps(execution[-1]), flush=True)
    if any(not row['passed'] for row in execution):
        raise ValueError('Actual consumer witness opcode execution failed')
    return attach_execution(ledger_path, execution)

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repo', 'build', 'validator', 'out'):
        parser.add_argument('--' + name, type=Path, required=True,
                            help='Explicit input or fresh output directory.')
    parser.add_argument('--source-root', type=Path,
                        help='Compiler snapshot for diagnostic phase provenance; defaults to repo owner.')
    parser.add_argument('--baseline', type=Path,
                        help='Optional preserved M5 CLI or build directory.')
    parser.add_argument('--plan', action='store_true',
                        help='Verify unchanged corpus/consumer; do not execute.')
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.repo = args.repo.resolve()
    args.build = args.build.resolve()
    args.validator = args.validator.resolve()
    args.out = args.out.resolve()
    cli = args.build / 'doe-compiler-c'
    consumer = args.repo / 'bench/gates/spirv_val_gate.py'
    ledger_path = args.repo / 'docs/c-migration/corpus-coverage-m5.json'
    if sha(consumer.read_bytes()) != CONSUMER_SHA256:
        raise ValueError('Actual unchanged consumer checksum differs')
    if sha(ledger_path.read_bytes()) != LEDGER_SHA256:
        raise ValueError('Frozen M5 corpus ledger checksum differs')
    previous = json.loads(ledger_path.read_text(encoding='utf-8'))['records']
    if len(previous) != 98 or len({r['source_sha256'] for r in previous}) != 91:
        raise ValueError('Frozen corpus is not 98 paths / 91 unique sources')
    classifications = {}
    for row in previous:
        raw = (args.repo / row['source']).read_bytes()
        if sha(raw) != row['source_sha256']:
            raise ValueError('Original corpus source changed: ' + row['source'])
        if row['source'] in WITNESSES:
            if sha(raw) != WITNESSES[row['source']]:
                raise ValueError('Six-witness checksum differs')
            classifications[row['source']] = dict(
                reason='Exact M6 compute witness; final four are no-op source files')
        elif row['admitted']:
            classifications[row['source']] = dict(reason='Retained M5 admission')
        else:
            classifications[row['source']] = missing_feature(raw.decode('utf-8'))
    if args.plan:
        print(json.dumps(dict(paths=98, unique_sources=91, expected_admitted=11,
                              classifications=classifications), indent=2))
        return 0
    args.out.mkdir(parents=True, exist_ok=False)
    spec = importlib.util.spec_from_file_location('unchanged_real_emitter', consumer)
    if spec is None or spec.loader is None:
        raise ImportError(str(consumer))
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    owner = (args.source_root.resolve() if args.source_root else
             args.repo / 'runtime/c/compiler') / 'src'
    owners = {p.name: p.read_text(encoding='utf-8') for p in owner.glob('*.c')}
    known = {}
    for is_uniform, rows in (
        (False, all_fixtures.admitted(args.repo)),
        (True, fixtures_m5.admitted(args.repo)),
    ):
        for name, source, expected in rows:
            known[sha(source)] = (is_uniform, name, expected)
    records = []
    for index, old in enumerate(previous):
        source_path = args.repo / old['source']
        source = source_path.read_bytes()
        expected_admitted = old['admitted'] or old['source'] in WITNESSES
        output_dir = args.out / f'source_{index:03d}'
        output_dir.mkdir()
        output = output_dir / 'module.spv'
        ok, message = gate.emit_spirv_for_wgsl(str(cli), source_path, output)
        errors = []
        if bool(ok) != bool(expected_admitted):
            errors.append(f'Expected admission={expected_admitted}; observed={ok}')
        if source_path.read_bytes() != source:
            errors.append('Consumer changed original WGSL')
        wanted = {'module.spv'} if ok else set()
        if {p.name for p in output_dir.iterdir()} != wanted:
            errors.append('Missing output, failure residue, or unexpected sidecar')
        validation, diagnostic, phase, diagnostic_owner = None, None, None, None
        stages = {key: 'passed' for key in
                  ('lexing', 'parsing', 'semantic_validation', 'emission')}
        if not ok:
            match = re.search(r'category=(\d+) offset=(\d+): (.*)', message)
            if not match:
                errors.append('Consumer failure has no compiler diagnostic')
                stages = {key: 'not_established' for key in stages}
            else:
                offset = int(match[2])
                prefix = source[:offset]
                diagnostic = dict(status=int(match[1]), offset=offset,
                                  line=prefix.count(b'\n') + 1,
                                  column=len(prefix.rsplit(b'\n', 1)[-1]) + 1,
                                  message=match[3])
                try:
                    phase, diagnostic_owner = diagnostic_phase(match[3], owners)
                except ValueError as exc:
                    errors.append(str(exc))
                    stages = {key: 'not_established' for key in stages}
                else:
                    reached = True
                    for key in stages:
                        if not reached:
                            stages[key] = 'not_reached'
                        elif key == phase:
                            stages[key] = 'rejected'
                            reached = False
        cpu_trials = 0
        if ok:
            command = [str(args.validator), '--target-env', 'vulkan1.1', str(output)]
            run = subprocess.run(command, capture_output=True, text=True,
                                 timeout=30, check=False)
            validation = dict(command=command, returncode=run.returncode,
                              stdout=run.stdout, stderr=run.stderr)
            if run.returncode:
                errors.append('Explicit Vulkan1.1 validation failed')
            if old['admitted']:
                is_uniform, _, expected = known[sha(source)]
                raw = output.read_bytes()
                words = list(struct.unpack('<' + 'I' * (len(raw) // 4), raw))
                try:
                    module = (UniformModule if is_uniform else StorageModule)(words)
                    module.interface(expected)
                    cpu_trials = (execute_vectors if is_uniform else run_vectors)(module, expected)
                except (ValueError, RuntimeError, AssertionError, KeyError, IndexError) as exc:
                    errors.append('Retained actual-output opcode execution: ' + str(exc))
        stages.update(
            external_vulkan11_validation=(
                'passed' if validation and not validation['returncode'] else
                'failed' if validation else 'not_run_no_module'),
            independent_instruction_execution=(
                'passed' if cpu_trials else 'pending_new_witness_stage' if ok else
                'not_run_no_module'),
            backend_qualification='not_run_no_physical_backend',
            runtime_adoption='not_run')
        row = dict(source=old['source'], source_sha256=sha(source),
                   source_bytes=len(source), admitted=ok,
                   expected_admitted=bool(expected_admitted),
                   first_rejection_stage=phase, diagnostic_owner=diagnostic_owner,
                   diagnostic=diagnostic,
                   original_source_at_diagnostic=(
                       source[diagnostic['offset']:diagnostic['offset'] + 100].decode(
                           'utf-8', errors='replace') if diagnostic else None),
                   real_consumer_message=message,
                   consumer_command=[str(cli), '--shader-path', str(source_path),
                                     '--out', str(output)],
                   output=str(output) if ok else None,
                   output_sha256=sha(output.read_bytes()) if ok else None,
                   validation=validation, stages=stages,
                   independent_execution_trials=cpu_trials,
                   static_feature_inspection=classifications[old['source']],
                   m5_first_rejection_stage=old['first_rejection_stage'],
                   errors=errors, passed=not errors)
        if args.baseline and ok:
            baseline = args.baseline.resolve()
            if baseline.is_dir():
                baseline /= 'doe-compiler-c'
            prior_output = output_dir / 'baseline.spv'
            prior_ok, prior_message = gate.emit_spirv_for_wgsl(
                str(baseline), source_path, prior_output)
            row['baseline_evidence'] = dict(
                admitted=prior_ok, message=prior_message,
                output_sha256=sha(prior_output.read_bytes()) if prior_ok else None,
                byte_identical=prior_output.read_bytes() == output.read_bytes()
                if prior_ok else None,
                acceptance='Cross-revision bytes recorded, not equality gate')
        records.append(row)
        write_json(args.out / 'records.json', records)
        if errors:
            print(json.dumps(row, sort_keys=True), flush=True)
    admitted = [row for row in records if row['admitted']]
    counts = dict(paths=len(records), unique_sources=91, admitted=len(admitted),
                  unique_admitted=len({r['source_sha256'] for r in admitted}),
                  validated=sum(bool(r['validation'] and not r['validation']['returncode'])
                                for r in records),
                  instruction_executed=sum(r['stages']['independent_instruction_execution'] == 'passed'
                                           for r in records),
                  first_rejection_stages=dict(collections.Counter(
                      r['first_rejection_stage'] for r in records if r['first_rejection_stage'])))
    report = dict(kind='M6 same98 phase-separated unchanged-consumer audit',
                  candidate_cli_sha256=sha(cli.read_bytes()),
                  consumer_sha256=CONSUMER_SHA256, frozen_ledger_sha256=LEDGER_SHA256,
                  validator_sha256=sha(args.validator.read_bytes()),
                  phase_mapping_source_hashes={k: sha(v.encode('utf-8')) for k, v in owners.items()},
                  classification_basis='Actual diagnostic uniquely source-bound to its compiler phase. Static feature inspection is separate.',
                  counts=counts, records=records,
                  failed=[r for r in records if not r['passed']],
                  notes=['No gate-main or EMIT-SKIP-as-pass.',
                         'No physical backend, runtime adoption or full migration claim.',
                         'Four newly admitted no-op sources are not texture/draw qualification.'])
    write_json(args.out / 'corpus-coverage-m6.json', report)
    if not report['failed']:
        report = execute_new_witnesses(args.repo, args.out / 'corpus-coverage-m6.json')
    write_json(args.out / 'results.json', report)
    print(json.dumps(report['counts'], indent=2), flush=True)
    return int(bool(report['failed']))


if __name__ == '__main__':
    raise SystemExit(main())
