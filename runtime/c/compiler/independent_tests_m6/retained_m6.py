"""Retain M4/M5 source semantics; admit only frozen M6 contract lifts.

Historical fixture and opcode-oracle modules are imported unchanged. Cross-revision
word hashes are evidence, not an equality gate. Current ABI1 and ABI2 storage
outputs must still be identical.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import hashlib
import json
import locale
from pathlib import Path
import struct
import subprocess
import sys

import all_fixtures
from api_m5 import Api, reflection_dict
from compute_oracle_m6 import Module as ComputeModule
from evaluate import Evaluator
import fixtures_m5
from rational_oracle import operation, self_test
from semantic_m5 import controls, execute_vectors, reflection_bytes
from typed_oracle import ExecutionError, OracleLimit, StructuralError
from uniform_oracle_m5 import BindingAdmissionError, Module

M4_LIFTS = {
    'unsupported_builtin': 'local_invocation_id is now supported.',
    'workgroup_extra_dimension': 'Two workgroup dimensions now admitted.',
    'wrong_component': 'Global .y scalar reads now admitted.',
    'empty_body': 'Empty compute entry body now admitted.',
    'alias_wrong_axis': 'Ordinary scalar local of global .y now admitted.',
    'wrong_global_component': 'Global .y index now admitted.',
    'local_as_offset': 'Scalar local admitted in index expression.',
    'local_as_condition_rhs': 'Scalar local admitted as comparison RHS.',
    'condition_shadow_alias': 'Comparison uses the visible scalar alias.',
    'index_offset_shadow_alias': 'Index uses the visible scalar alias.',
    'nodes_65': 'Unified scalar-node capacity is 320.',
    'dead_nodes_65': 'Unified scalar-node capacity is 320, including dead syntax.',
    'depth_binary_17': 'Unified scalar-expression depth is 18.',
    'depth_parentheses_17': 'Unified scalar-expression depth is 18.',
    'depth_max_17': 'Unified scalar-expression depth is 18.',
    **{'invalid_condition_' + str(i): 'Typed scalar comparison operand.'
       for i in (8, 12, 13, 14, 15, 16, 18)},
    **{'invalid_statement_' + str(i): 'Typed scalar storage index.'
       for i in (13, 14, 15, 16, 21, 22, 23)},
}
M5_LIFTS = {
    name: 'Typed u32 scalar expression in index/comparison context.'
    for name in ('member_index_offset', 'runtime_index_offset',
                 'expression_index', 'expression_condition',
                 'runtime_nonmember_rhs')
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n',
                    encoding='utf-8')



def lifted_expected(family: str, name: str) -> dict:
    """Independent fixed declarations of the exact historical lifted sources."""
    if family == 'm5':
        expected = dict(
            entry='main', n=[1, 1, 1], builtins=[28],
            structs=[fixtures_m5.shape('Cfg', [('a', 1), ('b', 2), ('c', 1)])],
            bindings=[fixtures_m5.uniform('p', 0, 0, 3),
                      fixtures_m5.storage('out', 1),
                      fixtures_m5.storage('fout', 2, scalar=2)])
    elif name in ('unsupported_builtin', 'workgroup_extra_dimension',
                  'wrong_component', 'empty_body'):
        expected = dict(entry='increment', n=[64, 1, 1], structs=[],
                        builtins=[27 if name == 'unsupported_builtin' else 28],
                        bindings=[fixtures_m5.storage('values', 0)])
    else:
        is_float = name in ('alias_wrong_axis', 'wrong_global_component')
        scalar = 2 if is_float else 1
        expected = dict(entry='main', n=[256 if is_float else 64, 1, 1],
                        builtins=[28], structs=[],
                        bindings=[fixtures_m5.storage('a', 0, scalar, 1),
                                  fixtures_m5.storage('b', 1, scalar, 1),
                                  fixtures_m5.storage('out', 2, scalar)])
    return expected


def execute_lift(words: list[int], family: str, name: str,
                 result_reflection: object) -> int:
    expected = lifted_expected(family, name)
    if bytes(result_reflection) != reflection_bytes(expected):
        raise ExecutionError('Exact lifted-source reflection differs')
    module = ComputeModule(words)
    module.interface(expected)
    maximum = 0xffffffff
    sentinel = 0xa55a36c9
    trials = 0
    # This is synthetic independent single-invocation boundary evidence, not a
    # whole-dispatch claim. Empty and unequal buffers test every nested guard.
    for x, y, z in ((0, 0, 0), (1, 2, 3), (3, 1, 0),
                    (maximum, maximum, maximum), (maximum - 1, 0, 1)):
        for lengths in ((0, 0, 0), (1, 0, 6), (4, 2, 6), (4, 2, 1)):
            uniforms = {}
            if family == 'm5':
                initial = {(0, 1): [sentinel] * lengths[2],
                           (0, 2): [sentinel] * 3}
                uniforms = {(0, 0): struct.pack('<III', x, 0x3f800000, y)}
                out_key = (0, 1)
            elif len(expected['bindings']) == 1:
                initial = {(0, 0): [3, maximum, 17, sentinel][:lengths[0]]}
                out_key = (0, 0)
            else:
                is_float = expected['bindings'][0]['scalar'] == 2
                initial = {
                    (0, 0): ([0x3f800000, 0xc0200000, 0x3f800001, 0x40800000]
                             if is_float else [0, 4, maximum, 7])[:lengths[0]],
                    (0, 1): ([0x40000000, 0x3f000000]
                             if is_float else [11, 13])[:lengths[1]],
                    (0, 2): [sentinel] * lengths[2]}
                out_key = (0, 2)
            wanted = {key: list(values) for key, values in initial.items()}

            def put(index: int, value: int) -> None:
                index &= maximum
                if index < len(wanted[out_key]):
                    wanted[out_key][index] = value & maximum

            def load(key: tuple[int, int], index: int) -> int:
                return initial[key][index] if index < len(initial[key]) else 0

            if family == 'm5':
                if name == 'member_index_offset':
                    put(x + y, 3)
                elif name == 'runtime_index_offset':
                    put(x + 1, 3)
                elif name == 'expression_index':
                    put(x, 3)
                # The two condition witnesses contain only return, no stores.
            elif name in ('unsupported_builtin', 'workgroup_extra_dimension', 'wrong_component'):
                index = y if name == 'wrong_component' else x
                put(index, load((0, 0), index) + 1)
            elif name in ('alias_wrong_axis', 'wrong_global_component'):
                index = y if name == 'alias_wrong_axis' else x
                put(index, operation(129, load((0, 0), y), load((0, 1), index)))
            elif name in ('nodes_65', 'depth_binary_17', 'depth_parentheses_17', 'depth_max_17'):
                put(0, {'nodes_65': 32, 'depth_binary_17': 17,
                        'depth_parentheses_17': 1, 'depth_max_17': 1}[name])
            elif name == 'local_as_offset':
                put(x + 1, 1)
            elif name == 'index_offset_shadow_alias':
                put(x, 1)
            elif name.startswith('invalid_statement_'):
                index = {13: x, 14: x + 3, 15: x - 3, 16: x,
                         21: y, 22: x + x, 23: x + x}[int(name.rsplit('_', 1)[1])]
                put(index, 1)
            elif name.startswith('invalid_condition_'):
                condition = {8: True, 12: load((0, 0), x) == 0,
                             13: ((x + 2) & maximum) == 0,
                             14: x == 0, 15: y == 0, 16: z == 0,
                             18: True}[int(name.rsplit('_', 1)[1])]
                if condition:
                    put(0, 1)
            elif name not in ('empty_body', 'dead_nodes_65',
                              'condition_shadow_alias', 'local_as_condition_rhs'):
                raise ValueError('No independent lifted-source oracle: ' + name)
            actual = {key: list(values) for key, values in initial.items()}
            before_uniforms = dict(uniforms)
            readonly = {(b['group'], b['binding']) for b in expected['bindings']
                        if b['kind'] == 1 and b['access'] == 1}
            module.execute(actual, {expected['builtins'][0]: (x, y, z)},
                           readonly, uniforms=uniforms)
            if actual != wanted or uniforms != before_uniforms:
                raise ExecutionError(f'Lift {name} x/y/z={(x,y,z)}: {actual} != {wanted}')
            trials += 1
    return trials

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('repo', 'build', 'validator', 'out'):
        parser.add_argument('--' + name, type=Path, required=True,
                            help='Explicit input or fresh output directory.')
    parser.add_argument('--baseline', type=Path,
                        help='Optional preserved library or build directory.')
    parser.add_argument('--plan', action='store_true',
                        help='Print the source inventory without loading candidate.')
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    args.repo = args.repo.resolve()
    library = args.build.resolve() / 'libdoe_compiler.so'
    positives = all_fixtures.admitted(args.repo)
    negatives = all_fixtures.rejected()
    uniform_positive = fixtures_m5.admitted(args.repo)
    uniform_negative = fixtures_m5.rejected()
    inventory = []
    for family, good, bad, lifts in (
        ('m4', positives, negatives, M4_LIFTS),
        ('m5', uniform_positive, uniform_negative, M5_LIFTS),
    ):
        for row in good:
            inventory.append(dict(family=family, case=row[0], admitted=True,
                                  source_sha256=sha(row[1])))
        for row in bad:
            inventory.append(dict(family=family, case=row[0],
                                  admitted=row[0] in lifts,
                                  source_sha256=sha(row[1])))
    frozen_signature = 'da527455725e281df006d0adddee98e8502aba3e57b05276c8301843603eed13'
    observed = sha(json.dumps(inventory, sort_keys=True, separators=(',', ':')).encode())
    if observed != frozen_signature:
        raise ValueError('Retained case inventory differs from preinspection freeze')
    if args.plan:
        print(json.dumps(dict(inventory=inventory, library=str(library)),
                         indent=2))
        return 0
    args.out.mkdir(parents=True, exist_ok=False)
    write_json(args.out / 'inventory.json', inventory)
    self_test()
    evaluator = Evaluator(library, args.out / 'modules', args.validator)
    api = Api(library)
    records = evaluator.records
    comparisons = []
    old = None
    if args.baseline:
        baseline = args.baseline.resolve()
        if baseline.is_dir():
            baseline /= 'libdoe_compiler.so'
        old = Api(baseline)
    for name, source, expected in positives:
        evaluator.valid(name, source, expected)
        status, _, words, errors = evaluator.call(source)
        vstatus, _, vwords, problems = api.call(source)
        errors.extend(problems)
        if status or vstatus or words != vwords:
            errors.append('Current ABI1/ABI2 storage output differs')
        evaluator.record('current_abi_modes_' + name, errors)
        if old:
            ostatus, _, owords, oerrors = old.call(source)
            comparisons.append(dict(
                case=name, baseline_status=ostatus, candidate_status=vstatus,
                source_sha256=sha(source),
                baseline_output_sha256=sha(struct.pack('<' + 'I' * len(owords), *owords)),
                candidate_output_sha256=sha(struct.pack('<' + 'I' * len(vwords), *vwords)),
                byte_identical=owords == vwords, baseline_errors=oerrors,
                acceptance='semantics required; cross-revision byte identity not required'))
    for name, source, offset, category in negatives:
        if name not in M4_LIFTS:
            evaluator.invalid(name, source, offset, category)
            status, result, _, errors = api.call(source)
            if not status:
                errors.append('Retained invalid source accepted through ABI2')
            if category is not None and status != category:
                errors.append('Retained ABI2 diagnostic category differs')
            if offset is not None and result.diagnostic.offset != offset:
                errors.append('Retained ABI2 original-source offset differs')
            evaluator.record('v2_negative_' + name, errors, status=status)
    compiled = {}
    for name, source, expected in uniform_positive:
        status, result, words, errors = api.call(source)
        detail = dict(source_sha256=sha(source), status=status,
                      diagnostic=result.diagnostic.message.decode(errors='replace'))
        if status:
            errors.append('Retained M5 admitted source rejected')
        else:
            if bytes(result.reflection) != reflection_bytes(expected):
                errors.append('M5 reflection differs including unused bytes: ' +
                              repr(reflection_dict(result.reflection)))
            raw = struct.pack('<' + 'I' * len(words), *words)
            path = args.out / 'modules' / ('m5_' + name + '.spv')
            path.write_bytes(raw)
            detail['output_sha256'] = sha(raw)
            val = subprocess.run(
                [str(args.validator), '--target-env', 'vulkan1.1', str(path)],
                capture_output=True, text=True, timeout=30, check=False)
            if val.returncode:
                errors.append('spirv-val: ' + val.stderr)
            try:
                module = Module(words)
                detail['structure'] = module.interface(expected)
                detail['cpu_trials'] = execute_vectors(module, expected)
                first = expected['vectors'][0]
                for key, minimum in module.uniform_minimums.items():
                    for length in (0, minimum - 1):
                        short = dict(first['uniforms'])
                        short[key] = short[key][:length]
                        initial = {k: list(v) for k, v in first['initial'].items()}
                        try:
                            module.execute(initial, first['x'], set(), uniforms=short)
                        except BindingAdmissionError:
                            pass
                        else:
                            raise ExecutionError('Short uniform admitted')
                        if initial != first['initial']:
                            raise ExecutionError('Short uniform mutated storage')
                compiled[name] = (words, expected)
            except (ExecutionError, StructuralError, OracleLimit, AssertionError,
                    KeyError, IndexError, ValueError) as exc:
                errors.append(type(exc).__name__ + ': ' + str(exc))
        evaluator.record('m5_' + name, errors, **detail)
    for name, source in uniform_negative:
        if name in M5_LIFTS:
            continue
        status, result, _, errors = api.call(source)
        if not status:
            errors.append('Retained M5 invalid source accepted')
        evaluator.record('m5_negative_' + name, errors, status=status,
                         diagnostic=result.diagnostic.message.decode(errors='replace'))
        status, _, _, errors = api.call(uniform_positive[0][1])
        if status:
            errors.append('Workspace did not recover after invalid source')
        evaluator.record('m5_reuse_' + name, errors)
    # Admission lifts are exact frozen old sources, not a wildcard exclusion.
    for family, rows, lifts in (
        ('m4', negatives, M4_LIFTS), ('m5', uniform_negative, M5_LIFTS),
    ):
        for name, source, *_ in rows:
            if name not in lifts:
                continue
            status, result, words, errors = api.call(source)
            detail = dict(source_sha256=sha(source), lift=lifts[name], status=status,
                          diagnostic=result.diagnostic.message.decode(errors='replace'))
            if status:
                errors.append('Exact intentional M6 admission lift rejected')
            else:
                path = args.out / 'modules' / (family + '_lift_' + name + '.spv')
                path.write_bytes(struct.pack('<' + 'I' * len(words), *words))
                val = subprocess.run(
                    [str(args.validator), '--target-env', 'vulkan1.1', str(path)],
                    capture_output=True, text=True, timeout=30, check=False)
                detail['output_sha256'] = sha(path.read_bytes())
                if val.returncode:
                    errors.append('spirv-val: ' + val.stderr)
                try:
                    detail['cpu_trials'] = execute_lift(words, family, name, result.reflection)
                    detail['execution_class'] = 'synthetic_single_invocation_boundaries'
                except (ExecutionError, StructuralError, OracleLimit, AssertionError,
                        KeyError, IndexError, ValueError) as exc:
                    errors.append(type(exc).__name__ + ': ' + str(exc))
                if family == 'm4':
                    old_status, _, old_words, old_errors = evaluator.call(source)
                    errors.extend(old_errors)
                    if old_status or old_words != words:
                        errors.append('Current ABI1/ABI2 lift output differs')
            evaluator.record(family + '_lift_' + name, errors, **detail)
    for name in ('all_members_3', 'shared_type_resources'):
        if name in compiled:
            for row in controls(*compiled[name]):
                errors = [] if row['passed'] else [repr(row)]
                evaluator.record('retained_mutation_' + name + '_' + row['case'],
                                 errors, control=row)
    for name in ('real_scale', 'real_relu', 'nodes_64', 'local_snapshot'):
        evaluator.boundary(next(s for n, s, _ in positives if n == name))
    selected = positives[-106:] + [(n, s, {}) for n, s, *_ in negatives[-69:]
                                 if n not in M4_LIFTS]
    baseline_calls = {n: evaluator.call(s) for n, s, _ in selected}

    def parallel(index: int) -> list[str]:
        name, source, _ = selected[index % len(selected)]
        status, result, words, errors = evaluator.call(source)
        reference = baseline_calls[name]
        if status != reference[0] or bytes(result) != bytes(reference[1]) or words != reference[2]:
            errors.append('Parallel nondeterminism ' + name)
        return errors

    with concurrent.futures.ThreadPoolExecutor(max_workers=16) as pool:
        errors = [error for result in pool.map(parallel, range(2800))
                  for error in result]
    evaluator.record('retained_parallel_2800_16_threads', errors)
    decimal_cases = [row for row in positives if row[0].startswith('decimal_')]
    saved_locale = locale.setlocale(locale.LC_NUMERIC)
    libc = ctypes.CDLL(None)
    libc.fegetround.restype = ctypes.c_int
    libc.fesetround.argtypes = [ctypes.c_int]
    original_round = libc.fegetround()
    available = subprocess.check_output(['locale', '-a'], text=True).splitlines()
    modes, errors = [], []
    try:
        for loc in available:
            try:
                locale.setlocale(locale.LC_NUMERIC, loc)
            except locale.Error:
                continue
            for rounding in (0, 0x400, 0x800, 0xc00):
                if libc.fesetround(rounding):
                    continue
                modes.append(dict(locale=loc, rounding=rounding))
                for name, source, _ in decimal_cases:
                    status, result, words, problems = evaluator.call(source)
                    reference = baseline_calls[name]
                    errors.extend(problems)
                    if status != reference[0] or bytes(result) != bytes(reference[1]) or words != reference[2]:
                        errors.append('Ambient dependence ' + name)
    finally:
        libc.fesetround(original_round)
        locale.setlocale(locale.LC_NUMERIC, saved_locale)
    evaluator.record('retained_decimal_locale_rounding', errors, modes=modes)
    mutation_command = [sys.executable, str(Path(__file__).with_name('test_oracle_m4.py')),
                        '--root', str(args.repo), '--modules', str(args.out / 'modules'),
                        '--out', str(args.out / 'm4-opcode-controls.json')]
    mutation_run = subprocess.run(mutation_command, capture_output=True, text=True,
                                  timeout=180, check=False)
    (args.out / 'm4-opcode-controls.log').write_text(
        mutation_run.stdout + mutation_run.stderr, encoding='utf-8')
    evaluator.record('retained_m4_opcode_controls',
                     [] if mutation_run.returncode == 0 else [mutation_run.stdout + mutation_run.stderr],
                     command=mutation_command, returncode=mutation_run.returncode)
    report = dict(checks=len(records), passed=sum(r['passed'] for r in records),
                  cpu_trials=sum(r.get('cpu_trials', 0) for r in records),
                  frozen_cases=len(inventory), exact_lifts=len(M4_LIFTS) + len(M5_LIFTS),
                  library_sha256=sha(library.read_bytes()), records=records,
                  baseline_evidence=comparisons, physical_execution=False)
    write_json(args.out / 'results.json', report)
    api.close()
    if old:
        old.close()
    print(json.dumps({k: v for k, v in report.items()
                      if k not in ('records', 'baseline_evidence')}, indent=2))
    return int(report['checks'] != report['passed'])


if __name__ == '__main__':
    raise SystemExit(main())
