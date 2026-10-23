"""Unchanged M7 corpus witnesses and source-order, raw-byte scalar oracles.

Expected resources, layouts and results come only from frozen source contracts.
Candidate source is neither imported nor used to calculate any expected value.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from api_job_m7 import JobApi
from api_m5 import reflection_dict
from compute_oracle_m7 import Module
from fixtures_m5 import shape, storage, uniform
from rational_oracle import operation
from semantic_m5 import reflection_bytes
from spirv_oracle_base import ExecutionError, OracleLimit, StructuralError

U = 0xffffffff
S = 0xa55a36c9
ONE = 0x3f800000
WITNESSES = {
    'bench/kernels/compilation-corpus/moderate_matmul_naive.wgsl':
        '691551e04ff875ec882b45993477ec40f432185921e3b0f7f77aec0e5e2b817b',
    'bench/inference-pipeline/kernels/dot_product_forward_f32.wgsl':
        'c177724439e1041b66f302e7e5399b1ac4cc85170c990616539a5fc639a925cc',
    'bench/inference-pipeline/kernels/dot_product_reverse_f32.wgsl':
        '95a309042cf65345e32adb33fc685e8f987cc98f174b41fdfb3c5cd68cc12d7a',
    'bench/inference-pipeline/kernels/matmul_logits_forward_f32.wgsl':
        'f8d513cd51712d32e8d750733ad25a34d423947ecf3d5b4cba08b2e30b32229f',
    'bench/inference-pipeline/kernels/matmul_logits_reverse_f32.wgsl':
        '272b02d1becb9333409e7cc8afd2a4ca41676b5c9600f01b540d30f7b6664b1d',
}
ADDITIONAL = {
    'bench/inference-pipeline/kernels/write_known_logits.wgsl':
        '4a0f3b417e16f4098a6f110c26deaf7258678a0971b5b0a35b32c45cbfd3c5a0',
}


def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()


def packed(values: list[int]) -> bytes:
    return struct.pack('<' + 'I' * len(values), *values)


def unpacked(raw: bytes) -> list[int]:
    if len(raw) % 4:
        raise ValueError('raw storage size must be a multiple of four')
    return list(struct.unpack('<' + 'I' * (len(raw) // 4), raw))


def load(values: list[int], index: int) -> int:
    return values[index] if 0 <= index < len(values) else 0


def ordered_dot(pairs: list[tuple[int, int]]) -> int:
    """Round each multiply and subsequent add separately in source order."""
    total = 0
    for left, right in pairs:
        total = operation(129, total, operation(133, left, right))
    return total


def vector(label: str, initial: dict, uniforms: dict, changes: dict,
           inputs: list[dict], synthetic: bool = False) -> dict:
    want = {key: list(value) for key, value in initial.items()}
    for key, items in changes.items():
        for index, value in items.items():
            if 0 <= index < len(want[key]):
                want[key][index] = value & U
    return dict(label=label, initial=initial, want=want,
                raw_initial={key: packed(value) for key, value in initial.items()},
                raw_want={key: packed(value) for key, value in want.items()},
                uniforms=uniforms, inputs=inputs[0], dispatch=inputs,
                synthetic=synthetic)


def dot_vectors(reverse: bool) -> list[dict]:
    values = [0x4c000000, 0xcc000000, ONE, 0x3f800001, 0xbf800000,
              0x3eaaaaab, 0x40800000]
    rhs_values = [ONE, ONE, ONE, 0x3f7ffffe, ONE, 0x40400000, 0x3e800000]
    cases = []
    # Cross-product freezes independent length/robustness, not a matched buffer.
    for count in [0, 1, 3, 7]:
        for left_size, right_size in [(0, 0), (0, 7), (7, 0), (1, 4),
                                      (7, 2), (7, 7)]:
            left, right = values[:left_size], rhs_values[:right_size]
            order = range(count - 1, -1, -1) if reverse else range(count)
            total = ordered_dot([(load(left, i), load(right, i)) for i in order])
            for output_size in [0, 1, 4]:
                cases.append(vector(
                    f'n{count}_left{left_size}_right{right_size}_out{output_size}',
                    {(0, 1): left, (0, 2): right, (0, 3): [S] * output_size},
                    {(0, 0): packed([count, 0xfedcba98, 0x01234567, U])},
                    {(0, 3): {0: total}}, [{28: (0, 0, 0)}, {28: (1, 0, 0)}]))
    for x in [1, U]:
        cases.append(vector('early_return_' + str(x),
                            {(0, 1): values, (0, 2): rhs_values, (0, 3): [S] * 4},
                            {(0, 0): packed([7, 1, 2, 3])}, {},
                            [{28: (x, 37, 91)}], synthetic=True))
    left = ([0x3f800001, 0xbf800000, 0x3eaaaaab, 0x4c000000,
             0xcc000000, ONE, 0x3f000000] * 37)[:257]
    right = ([0x3f7ffffe, ONE, 0x40400000, ONE, ONE, ONE, 0x40000000] * 37)[:257]
    order = range(256, -1, -1) if reverse else range(257)
    total = ordered_dot([(left[i], right[i]) for i in order])
    cases.append(vector('long_257_source_order',
                        {(0, 1): left, (0, 2): right, (0, 3): [S] * 3},
                        {(0, 0): packed([257, 99, 98, 97])},
                        {(0, 3): {0: total}}, [{28: (0, 0, 0)}]))
    return cases


def naive_vectors() -> list[dict]:
    a = [0x4c000000, 0xcc000000, ONE, 0x3f800001, 0xbf800000,
         0x3f000000, 0x40400000, 0xc0000000, 0x3eaaaaab]
    b = [ONE, 0x40000000, ONE, ONE, ONE, 0x3f7ffffe,
         0x40400000, 0xbf800000, 0x3f000000, ONE, ONE, ONE]
    cases = []
    for m, n, k in [(0, 2, 3), (3, 0, 3), (3, 2, 0), (3, 2, 1), (3, 2, 3)]:
        for alen, blen in [(0, 0), (0, 12), (9, 0), (1, 7), (9, 2), (9, 12)]:
            left, right = a[:alen], b[:blen]
            for output_size in [0, 1, 6, 10]:
                changes = {}
                for row in range(m):
                    for col in range(n):
                        changes[row * n + col] = ordered_dot([
                            (load(left, (row * k + p) & U),
                             load(right, (p * n + col) & U)) for p in range(k)])
                dispatch = [{28: (x, y, 0)} for y in range(8) for x in range(8)]
                cases.append(vector(f'm{m}_n{n}_k{k}_a{alen}_b{blen}_out{output_size}',
                                    {(0, 0): left, (0, 1): right,
                                     (0, 2): [S] * output_size},
                                    {(0, 3): packed([m, n, k])},
                                    {(0, 2): changes}, dispatch))
    # Source u32 products wrap before every independently guarded access.
    for row, col, m, n, k in [(0x80000000, 0, U, 2, 2),
                               (0x80000000, 1, U, 2, 2),
                               (0, 0, 1, 0x80000000, 3)]:
        result = ordered_dot([(load(a, (row * k + p) & U),
                               load(b, (p * n + col) & U)) for p in range(k)])
        cases.append(vector(f'wrapped_row{row}_col{col}_n{n}',
                            {(0, 0): a, (0, 1): b, (0, 2): [S] * 4},
                            {(0, 3): packed([m, n, k])},
                            {(0, 2): {(row * n + col) & U: result}},
                            [{28: (col, row, 3)}], synthetic=True))
    return cases


def logits_vectors(reverse: bool) -> list[dict]:
    hidden = [0x4c000000, 0xcc000000, ONE, 0x3f800001, 0xbf800000,
              0x3f000000, 0x3eaaaaab]
    weights = [ONE, ONE, ONE, 0x3f7ffffe, ONE, 0x40000000, 0x40400000,
               0xbf800000, 0x40000000, 0xc0000000, 0x3f000000, ONE,
               0x3e800000, 0x3f800001, 0x3f800000, 0xbf800000, ONE,
               0x40400000, 0x3f7ffffe, ONE, 0x3f000000]
    cases = []
    for rows, cols in [(0, 3), (3, 0), (3, 1), (3, 3), (3, 7)]:
        for hlen, wlen in [(0, 0), (0, 21), (7, 0), (1, 7), (7, 2), (7, 21)]:
            left, right = hidden[:hlen], weights[:wlen]
            order = list(range(cols - 1, -1, -1) if reverse else range(cols))
            changes = {row: ordered_dot([(load(left, col),
                                         load(right, (row * cols + col) & U))
                                        for col in order]) for row in range(rows)}
            for olen in [0, 1, 3, 6]:
                inputs = [{27: (0, 0, 0), 26: (row, 0, 0)} for row in range(4)]
                cases.append(vector(f'rows{rows}_cols{cols}_h{hlen}_w{wlen}_out{olen}',
                                    {(0, 1): left, (0, 2): right, (0, 3): [S] * olen},
                                    {(0, 0): packed([rows, cols, 0xfedcba98, U])},
                                    {(0, 3): changes}, inputs))
    for lid, wid in [(1, 0), (U, 0), (0, U), (1, U)]:
        cases.append(vector(f'early_return_lid{lid}_wid{wid}',
                            {(0, 1): hidden, (0, 2): weights, (0, 3): [S] * 5},
                            {(0, 0): packed([3, 7, 1, 2])}, {},
                            [{27: (lid, 37, 91), 26: (wid, 17, 29)}],
                            synthetic=True))
    return cases


def positives(repo: Path, include_additional: bool = True) -> list[tuple]:
    cases = []
    for name, digest in (WITNESSES | (ADDITIONAL if include_additional else {})).items():
        raw = (repo / name).read_bytes()
        if sha(raw) != digest:
            raise ValueError('Unchanged witness source hash differs: ' + name)
        stem = Path(name).stem
        expected = dict(entry='main', n=[1, 1, 1], source=name)
        if stem == 'moderate_matmul_naive':
            expected.update(n=[8, 8, 1], builtins=[28],
                            structs=[shape('Dims', [('M', 1), ('N', 1), ('K', 1)])],
                            bindings=[storage('a', 0, 2, 1), storage('b', 1, 2, 1),
                                      storage('c', 2, 2), uniform('dims', 3, 0, 3)],
                            vectors=naive_vectors())
        elif stem.startswith('dot_product_'):
            expected.update(builtins=[28],
                            structs=[shape('Params', [('length', 1), ('_pad0', 1),
                                                     ('_pad1', 1), ('_pad2', 1)])],
                            bindings=[uniform('params', 0, 0, 4), storage('lhs', 1, 2, 1),
                                      storage('rhs', 2, 2, 1), storage('output', 3, 2)],
                            vectors=dot_vectors('reverse' in stem))
        elif stem.startswith('matmul_logits_'):
            expected.update(builtins=[27, 26],
                            structs=[shape('Params', [('rows', 1), ('cols', 1),
                                                     ('_pad0', 1), ('_pad1', 1)])],
                            bindings=[uniform('params', 0, 0, 4), storage('hidden', 1, 2, 1),
                                      storage('weights', 2, 2, 1), storage('output', 3, 2)],
                            vectors=logits_vectors('reverse' in stem))
        else:
            expected.update(builtins=[26], structs=[], bindings=[storage('output', 0, 2)],
                            vectors=[vector(f'known_out{n}', {(0, 0): [S] * n}, {},
                                            {(0, 0): {0: 0x411a8000, 1: 0x411b4000}},
                                            [{26: (x, 0, 0)} for x in range(4)])
                                     for n in [0, 1, 2, 5]])
        cases.append(('real_' + stem, raw, expected))
    return cases


def execute_vectors(module: Module, expected: dict) -> dict:
    """Consume independently packed bytes; compare all output and input bytes."""
    readonly = {(b['group'], b['binding']) for b in expected['bindings']
                if b['kind'] == 1 and b['access'] == 1}
    invocations = synthetic = dispatches = storage_accesses = 0
    for case in expected['vectors']:
        actual = {key: unpacked(raw) for key, raw in case['raw_initial'].items()}
        uniforms = {key: bytes(raw) for key, raw in case['uniforms'].items()}
        for inputs in case['dispatch']:
            accesses = module.execute(actual, inputs, readonly, uniforms)
            invocations += 1
            synthetic += int(case['synthetic'])
            storage_accesses += sum(a[0] in {'read', 'write'} for a in accesses)
        dispatches += int(not case['synthetic'])
        raw_actual = {key: packed(value) for key, value in actual.items()}
        if raw_actual != case['raw_want']:
            bad = {str(key): {'actual': value.hex(),
                             'want': case['raw_want'][key].hex()}
                   for key, value in raw_actual.items()
                   if value != case['raw_want'][key]}
            raise ExecutionError(f"{case['label']}: packed buffers differ {bad}")
        if uniforms != case['uniforms']:
            raise ExecutionError(case['label'] + ': uniform bytes mutated')
    return dict(cpu_invocations=invocations, synthetic_single_invocations=synthetic,
                coherent_dispatch_trials=dispatches, resource_vectors=len(expected['vectors']),
                observed_storage_accesses=storage_accesses)


def reflection_json(expected: dict) -> dict:
    def scalar(value: int) -> str | None:
        return {0: None, 1: 'u32', 2: 'f32'}[value]
    bindings = []
    for spec in expected['bindings']:
        item = dict(spec)
        item['kind'] = {1: 'storage_buffer', 2: 'uniform_buffer'}[spec['kind']]
        item['access'] = {1: 'read', 2: 'read_write'}[spec['access']]
        item['scalar'] = scalar(spec['scalar'])
        item['struct_index'] = None if spec['struct_index'] == U else spec['struct_index']
        bindings.append(item)
    structs = [dict(spec, members=[dict(m, scalar=scalar(m['scalar']))
                                  for m in spec['members']]) for spec in expected['structs']]
    return dict(schema_version=2, entry_point=expected['entry'], workgroup_size=expected['n'],
                binding_count=len(bindings), bindings=bindings,
                struct_count=len(structs), structs=structs)


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def check_module(raw: bytes, path: Path, validator: Path, expected: dict) -> dict:
    path.write_bytes(raw)
    command = [str(validator), '--target-env', 'vulkan1.1', str(path)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=30, check=False)
    if result.returncode:
        raise ValueError('Vulkan1.1 validation failed: ' + result.stderr)
    module = Module(unpacked(raw))
    structure = module.interface(expected)
    proof = execute_vectors(module, expected)
    return dict(output_sha256=sha(raw), word_count=len(raw) // 4,
                validation=dict(command=command, returncode=result.returncode,
                                stdout=result.stdout, stderr=result.stderr),
                structure=structure, **proof)


def fixture_summary(repo: Path) -> dict:
    rows = []
    for name, source, expected in positives(repo):
        # Bind every byte, uniform, input and expected result without huge JSON.
        rows.append(dict(case=name, source=expected['source'], source_sha256=sha(source),
                         expected_reflection=reflection_json(expected),
                         vectors=len(expected['vectors']),
                         vector_sha256=sha(repr(expected['vectors']).encode('utf-8')),
                         invocations=sum(len(v['dispatch']) for v in expected['vectors'])))
    return dict(kind='M7 unchanged-source frozen host expectations',
                numerical_oracle='Fraction exact binary32 multiply then add, source order',
                physical_backend='not_run', fixtures=rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['repo', 'library', 'cli', 'validator', 'out']:
        parser.add_argument('--' + name, type=Path, required=name == 'repo',
                            help='Explicit ' + name + ' path.')
    parser.add_argument('--plan', action='store_true', help='Source-only fixtures, no candidate use.')
    args = parser.parse_args()
    if args.plan:
        print(json.dumps(fixture_summary(args.repo), indent=2, sort_keys=True))
        return 0
    if any(getattr(args, key) is None for key in ['library', 'cli', 'validator', 'out']):
        parser.error('execution requires --library, --cli, --validator and --out')
    args.out.mkdir(parents=True, exist_ok=False)
    records = []
    api = JobApi(args.library)
    try:
        for name, source, expected in positives(args.repo):
            directory = args.out / name
            directory.mkdir()
            row = dict(case=name, source=expected['source'], source_sha256=sha(source),
                       capability_lift='M7 job and binary/reflection-v2 CLI only',
                       physical_backend='not_run', runtime_adoption='not_run', modes={})
            errors = []
            api_raw = None
            status, result, words, api_errors = api.call(source)
            errors.extend(api_errors)
            row['job_status'] = status
            row['job_diagnostic'] = result.diagnostic.message.decode('utf-8', 'replace')
            if status:
                errors.append('Job API rejected unchanged witness')
            else:
                api_raw = packed(words)
                if bytes(result.reflection) != reflection_bytes(expected):
                    errors.append('Exact native reflection differs: ' + repr(reflection_dict(result.reflection)))
                try:
                    row['modes']['job'] = check_module(api_raw, directory / 'job.spv', args.validator, expected)
                except (ExecutionError, OracleLimit, StructuralError, ValueError,
                        RuntimeError, KeyError, IndexError, TypeError, AssertionError) as exc:
                    errors.append('job: ' + type(exc).__name__ + ': ' + str(exc))
            for mode in ['binary', 'v2']:
                mode_dir = directory / mode
                mode_dir.mkdir()
                output = mode_dir / 'module.spv'
                command = [str(args.cli), '--shader-path', str(args.repo / expected['source']), '--out', str(output)]
                if mode == 'v2':
                    command += ['--reflection-v2', str(mode_dir / 'reflection.json')]
                run = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False)
                receipt = dict(command=command, returncode=run.returncode, stdout=run.stdout, stderr=run.stderr)
                if run.returncode:
                    errors.append(mode + ': unchanged witness rejected')
                else:
                    try:
                        raw = output.read_bytes()
                        receipt.update(check_module(raw, output, args.validator, expected))
                        if api_raw is None or raw != api_raw:
                            errors.append(mode + ': current words differ from Job API')
                        wanted_files = {'module.spv'}
                        if mode == 'v2':
                            wanted_files.add('reflection.json')
                            if json.loads((mode_dir / 'reflection.json').read_text(encoding='utf-8')) != reflection_json(expected):
                                errors.append('v2: exact JSON schema/values differ')
                        if {p.name for p in mode_dir.iterdir()} != wanted_files:
                            errors.append(mode + ': unexpected sidecar or residue')
                    except (ExecutionError, OracleLimit, StructuralError, ValueError,
                            RuntimeError, KeyError, IndexError, TypeError, AssertionError, OSError) as exc:
                        errors.append(mode + ': ' + type(exc).__name__ + ': ' + str(exc))
                row['modes'][mode] = receipt
            if (args.repo / expected['source']).read_bytes() != source:
                errors.append('Original source modified')
            row.update(passed=not errors, errors=errors)
            records.append(row)
            write_json(args.out / 'records.json', records)
            print(json.dumps(row) if errors else 'PASS ' + name, flush=True)
    finally:
        api.close()
    report = dict(kind='M7 unchanged witness native/API/CLI acceptance',
                  fixtures=fixture_summary(args.repo), records=records,
                  passed=all(row['passed'] for row in records),
                  candidate_library_sha256=sha(args.library.read_bytes()),
                  candidate_cli_sha256=sha(args.cli.read_bytes()),
                  validator_sha256=sha(args.validator.read_bytes()),
                  failures=[row['case'] for row in records if not row['passed']])
    write_json(args.out / 'results.json', report)
    return int(not report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
