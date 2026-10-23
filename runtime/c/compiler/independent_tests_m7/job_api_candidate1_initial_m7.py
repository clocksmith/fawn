"""Independent consumer of the frozen M7 caller-owned job ABI.

Only public headers/contract were consulted while freezing this module.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
import hashlib
import json
from pathlib import Path
import re

from api_m5 import Requirements, Result, Reflection
from evaluate import Diagnostic, U32, SIZE, SENTINEL

ABI = 3
OK, INPUT_ERROR, PARSE_ERROR, UNSUPPORTED, SEMANTIC_ERROR, OUTPUT_TOO_SMALL, ABI_MISMATCH = range(7)


class Capacities(C.Structure):
    _fields_ = [(name, SIZE) for name in ('source_bytes', 'tokens', 'statements', 'expressions', 'words')] + [('expression_depth', U32), ('control_depth', U32)]


DEFAULT_CAPACITIES = dict(source_bytes=65536, tokens=8192, statements=1024,
                          expressions=4096, words=65536,
                          expression_depth=32, control_depth=16)


def capacities(**changes: int) -> Capacities:
    """Return an independent CLI-envelope job capacities value."""
    return Capacities(**(DEFAULT_CAPACITIES | changes))


def copy_caps(value: Capacities, **changes: int) -> Capacities:
    return Capacities(**({name: getattr(value, name) for name, _ in value._fields_} | changes))


def cap_dict(value: Capacities) -> dict:
    return {name: getattr(value, name) for name, _ in value._fields_}


class JobApi:
    def __init__(self, library: Path, caps: Capacities | None = None,
                 extra_workspace: int = 0) -> None:
        self.lib = C.CDLL(str(Path(library).resolve()))
        self.query = self.lib.doe_compiler_job_requirements
        self.query.argtypes = [U32, C.POINTER(Capacities), SIZE, C.POINTER(Requirements), SIZE]
        self.query.restype = C.c_int
        self.compile = self.lib.doe_compiler_compile_job
        self.compile.argtypes = [U32, C.POINTER(Capacities), SIZE, C.c_void_p, SIZE,
                                 C.c_void_p, SIZE, C.POINTER(U32), SIZE,
                                 C.POINTER(Result), SIZE]
        self.compile.restype = C.c_int
        self.caps = copy_caps(caps) if caps is not None else capacities()
        self.req = Requirements()
        status = self.query(ABI, C.byref(self.caps), C.sizeof(self.caps), C.byref(self.req), C.sizeof(self.req))
        if status != OK:
            raise AssertionError(f'valid capacities query failed: {status}, {cap_dict(self.caps)}')
        if not self.req.size_bytes or not self.req.alignment_bytes or self.req.alignment_bytes & (self.req.alignment_bytes - 1):
            raise AssertionError('invalid workspace size/alignment')
        self.workspace_size = self.req.size_bytes + extra_workspace
        self.backing = C.create_string_buffer(self.workspace_size + self.req.alignment_bytes + 64)
        base = C.addressof(self.backing) + 32
        self.workspace = (base + self.req.alignment_bytes - 1) & ~(self.req.alignment_bytes - 1)
        C.memset(C.addressof(self.backing), 0xA5, C.sizeof(self.backing))
        self.prefix = C.string_at(C.addressof(self.backing), self.workspace - C.addressof(self.backing))
        self.suffix_address = self.workspace + self.workspace_size
        self.suffix_size = C.addressof(self.backing) + C.sizeof(self.backing) - self.suffix_address
        self.suffix = C.string_at(self.suffix_address, self.suffix_size)

    def close(self) -> None:
        self.workspace = None
        self.backing = None

    def call(self, source: bytes, capacity: int | None = None) -> tuple:
        if capacity is None:
            capacity = self.caps.words
        src = C.create_string_buffer(source, max(1, len(source)))
        words = (U32 * (capacity + 16))(*([SENTINEL] * (capacity + 16)))
        before = bytes(words)
        result_backing = C.create_string_buffer(C.sizeof(Result) + 64)
        C.memset(C.addressof(result_backing), 0xA5, C.sizeof(result_backing))
        result = Result.from_buffer(result_backing, 32)
        caps_before = bytes(self.caps)
        status = self.compile(ABI, C.byref(self.caps), C.sizeof(self.caps),
                              self.workspace, self.workspace_size, src, len(source),
                              C.cast(C.byref(words, 32), C.POINTER(U32)), capacity,
                              C.byref(result), C.sizeof(result))
        errors = []
        if bytes(src)[:len(source)] != source:
            errors.append('borrowed source mutated')
        if bytes(self.caps) != caps_before:
            errors.append('borrowed capacities mutated')
        if bytes(result_backing)[:32] != b'\xa5'*32 or bytes(result_backing)[-32:] != b'\xa5'*32:
            errors.append('result guards modified')
        if bytes(words)[:32] != before[:32] or bytes(words)[32 + capacity * 4:] != before[32 + capacity * 4:]:
            errors.append('word output guards modified')
        if C.string_at(C.addressof(self.backing), len(self.prefix)) != self.prefix or C.string_at(self.suffix_address, self.suffix_size) != self.suffix:
            errors.append('workspace guards modified')
        if status:
            if bytes(words) != before:
                errors.append('failure published words')
            if result.word_count or bytes(result.reflection) != bytes(C.sizeof(Reflection)):
                errors.append('failure published count/reflection')
            d = result.diagnostic
            if d.status != status or d.offset > len(source) or d.line < 1 or d.column < 1:
                errors.append('invalid original-source diagnostic')
            elif b'\r' not in source:
                prefix = source[:d.offset]
                if (d.line, d.column) != (prefix.count(b'\n') + 1, len(prefix.rsplit(b'\n', 1)[-1]) + 1):
                    errors.append('diagnostic coordinates differ from original source')
            if 0 not in C.string_at(C.addressof(d) + Diagnostic.message.offset, 192):
                errors.append('diagnostic message is not bounded')
        elif not 0 < result.word_count <= capacity:
            errors.append('success word count invalid')
        elif bytes(words)[32+result.word_count*4:] != before[32+result.word_count*4:]:
            errors.append('success modified words after published count')
        return status, result, list(words[8:8 + min(result.word_count, capacity)]) if not status else [], errors


EMPTY = b'@compute @workgroup_size(1) fn main() {}'
STORE = b'@group(0) @binding(0) var<storage,read_write> out:array<u32>; @compute @workgroup_size(1) fn main(){out[0u]=1u;}'


def diagnostic(result: Result) -> dict:
    d = result.diagnostic
    return dict(status=d.status, offset=d.offset, line=d.line, column=d.column,
                message=bytes(d.message).decode('utf-8', 'replace'))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def query_checks(api: JobApi) -> list[dict]:
    rows = []
    max_size = (1 << (C.sizeof(SIZE) * 8)) - 1
    trials = [('wrong_version', 2, api.caps, C.sizeof(Capacities), C.sizeof(Requirements), ABI_MISMATCH),
              ('short_caps', ABI, api.caps, C.sizeof(Capacities)-1, C.sizeof(Requirements), ABI_MISMATCH),
              ('long_caps', ABI, api.caps, C.sizeof(Capacities)+1, C.sizeof(Requirements), ABI_MISMATCH),
              ('short_requirements', ABI, api.caps, C.sizeof(Capacities), C.sizeof(Requirements)-1, ABI_MISMATCH),
              ('long_requirements', ABI, api.caps, C.sizeof(Capacities), C.sizeof(Requirements)+1, ABI_MISMATCH),
              ('null_caps', ABI, None, C.sizeof(Capacities), C.sizeof(Requirements), INPUT_ERROR)]
    invalid = [('source_u32_bound', 'source_bytes', 0xffffffff), ('source_size_overflow', 'source_bytes', max_size),
               ('tokens_add_overflow', 'tokens', max_size), ('statement_multiply_overflow', 'statements', max_size),
               ('expression_multiply_overflow', 'expressions', max_size), ('word_multiply_overflow', 'words', max_size),
               ('derived_id_bound_expression', 'expressions', 0x10000000), ('derived_id_bound_statement', 'statements', 0x10000000),
               ('expression_depth_zero', 'expression_depth', 0), ('expression_depth_above', 'expression_depth', 65),
               ('control_depth_zero', 'control_depth', 0), ('control_depth_above', 'control_depth', 33)]
    trials += [(name, ABI, copy_caps(api.caps, **{field: value}), C.sizeof(Capacities), C.sizeof(Requirements), INPUT_ERROR)
               for name, field, value in invalid]
    for name, version, caps, cs, rs, expected in trials:
        req = Requirements()
        C.memset(C.byref(req), 0xA5, C.sizeof(req))
        before = bytes(req)
        status = api.query(version, C.byref(caps) if caps is not None else None, cs, C.byref(req), rs)
        require(status == expected, f'{name}: query status {status}, expected {expected}')
        require(bytes(req) == before, f'{name}: failed query wrote requirements')
        rows.append(dict(name=name, status=status))
    require(api.query(ABI, C.byref(api.caps), C.sizeof(api.caps), None, C.sizeof(Requirements)) == INPUT_ERROR, 'null query result')
    for name, caps in [('zero_counts', capacities(source_bytes=0, tokens=0, statements=0, expressions=0, words=0, expression_depth=1, control_depth=1)),
                       ('maximum_depths', capacities(expression_depth=64, control_depth=32)),
                       ('maximum_source', capacities(source_bytes=0xfffffffe))]:
        req = Requirements()
        status = api.query(ABI, C.byref(caps), C.sizeof(caps), C.byref(req), C.sizeof(req))
        require(status == OK and req.size_bytes > 0, f'{name}: valid query rejected {status}')
        require(req.alignment_bytes <= C.alignment(C.c_longdouble), f'{name}: over-aligned workspace')
        rows.append(dict(name=name, status=status, size=req.size_bytes, alignment=req.alignment_bytes))
    return rows


def preflight_checks(api: JobApi) -> list[dict]:
    rows = []
    source = C.create_string_buffer(STORE, len(STORE))
    words = (U32 * 64)(*([SENTINEL] * 64))
    result = Result()
    good_caps = api.caps
    bad_caps = copy_caps(api.caps, control_depth=0)
    trials = [('wrong_version', dict(version=1), ABI_MISMATCH, True),
              ('short_caps', dict(caps_size=C.sizeof(Capacities)-1), ABI_MISMATCH, True),
              ('long_caps', dict(caps_size=C.sizeof(Capacities)+1), ABI_MISMATCH, True),
              ('short_result', dict(result_size=C.sizeof(Result)-1), ABI_MISMATCH, True),
              ('long_result', dict(result_size=C.sizeof(Result)+1), ABI_MISMATCH, True),
              ('null_result', dict(result=None), INPUT_ERROR, True),
              ('null_caps', dict(caps=None), INPUT_ERROR, False),
              ('bad_caps', dict(caps=C.byref(bad_caps)), INPUT_ERROR, False),
              ('null_workspace', dict(workspace=None), INPUT_ERROR, False),
              ('short_workspace', dict(workspace_size=api.req.size_bytes-1), INPUT_ERROR, False),
              ('misaligned_workspace', dict(workspace=api.workspace+1), INPUT_ERROR, False),
              ('null_source_positive_length', dict(source=None), INPUT_ERROR, False),
              ('null_words_positive_capacity', dict(words=None), INPUT_ERROR, False)]
    for name, changed, expected, untouched in trials:
        C.memset(C.byref(result), 0xA5, C.sizeof(result))
        C.memset(api.workspace, 0xA5, api.workspace_size)
        before = bytes(result), bytes(words), C.string_at(api.workspace, api.workspace_size)
        args = dict(version=ABI, caps=C.byref(good_caps), caps_size=C.sizeof(good_caps),
                    workspace=api.workspace, workspace_size=api.workspace_size, source=source,
                    source_length=len(STORE), words=words, word_capacity=64,
                    result=C.byref(result), result_size=C.sizeof(result)) | changed
        status = api.compile(*args.values())
        require(status == expected, f'{name}: status {status}, expected {expected}')
        require(bytes(words) == before[1], f'{name}: wrote words')
        require(C.string_at(api.workspace, api.workspace_size) == before[2], f'{name}: accessed workspace before preflight')
        if untouched:
            require(bytes(result) == before[0], f'{name}: wrote result')
        else:
            require(result.word_count == 0 and bytes(result.reflection) == bytes(C.sizeof(Reflection)), f'{name}: published result')
            require(result.diagnostic.offset == 0 and result.diagnostic.status == expected, f'{name}: invalid preflight diagnostic')
        rows.append(dict(name=name, status=status))
    return rows


def budget_checks(library: Path) -> list[dict]:
    rows = []
    # Exact lexical token accounting, excluding reserved end-token.
    empty_tokens = 14  # @ compute @ workgroup_size ( 1 ) fn main ( ) { }
    # Count is independently lexical, not learned from implementation.
    empty_tokens = len(re.findall(rb'[A-Za-z_][A-Za-z_0-9]*|[0-9]+|[^\s]', EMPTY))
    tests = [('source_exact', EMPTY, dict(source_bytes=len(EMPTY)), OK, ''),
             ('source_short', EMPTY, dict(source_bytes=len(EMPTY)-1), INPUT_ERROR, 'source'),
             ('token_exact', EMPTY, dict(tokens=empty_tokens), OK, ''),
             ('token_short', EMPTY, dict(tokens=empty_tokens-1), INPUT_ERROR, 'token'),
             ('empty_zero_statements_nodes', EMPTY, dict(statements=0, expressions=0), OK, ''),
             ('zero_source', EMPTY, dict(source_bytes=0), INPUT_ERROR, 'source'),
             ('zero_tokens', EMPTY, dict(tokens=0), INPUT_ERROR, 'token'),
             ('zero_words', EMPTY, dict(words=0), OUTPUT_TOO_SMALL, ''),
             ('one_statement_exact', EMPTY[:-1]+b';}', dict(statements=1, expressions=0), OK, ''),
             ('one_statement_short', EMPTY[:-1]+b';}', dict(statements=0), INPUT_ERROR, 'statement'),
             ('return_zero_nodes', EMPTY[:-1]+b'return;}', dict(statements=1, expressions=0), OK, ''),
             ('generated_var_zero_nodes', EMPTY[:-1]+b'var x:u32;}', dict(statements=1, expressions=0), OK, ''),
             ('continuing_four_statements', EMPTY[:-1]+b'loop {break; continuing {break if true;}}}', dict(statements=4), OK, ''),
             ('continuing_short_statements', EMPTY[:-1]+b'loop {break; continuing {break if true;}}}', dict(statements=3), INPUT_ERROR, 'statement'),
             ('node_exact', STORE, dict(expressions=2), OK, ''),
             ('node_short', STORE, dict(expressions=1), INPUT_ERROR, 'expression'),
             ('depth_leaf', STORE, dict(expression_depth=1), OK, ''),
             ('depth_paren_short', STORE.replace(b'=1u', b'=(1u)'), dict(expression_depth=1), INPUT_ERROR, 'depth'),
             ('control_block_exact', EMPTY[:-1]+b'{} }', dict(control_depth=1), OK, ''),
             ('control_block_short', EMPTY[:-1]+b'{{}} }', dict(control_depth=1), INPUT_ERROR, 'depth'),
             ('depth64_exact', STORE.replace(b'=1u', b'='+b'('*63+b'1u'+b')'*63), dict(expression_depth=64), OK, ''),
             ('depth65_exhausted', STORE.replace(b'=1u', b'='+b'('*64+b'1u'+b')'*64), dict(expression_depth=64), INPUT_ERROR, 'depth'),
             ('control32_exact', EMPTY[:-1]+b'{'*32+b'}'*32+b'}', dict(control_depth=32), OK, ''),
             ('control33_exhausted', EMPTY[:-1]+b'{'*33+b'}'*33+b'}', dict(control_depth=32), INPUT_ERROR, 'depth'),
             ('combined_control32_expression64', EMPTY[:-1]+b'{'*32+b'let a='+b'('*63+b'1u'+b')'*63+b';'+b'}'*32+b'}', dict(control_depth=32, expression_depth=64), OK, ''),
             ('combined_control32_expression65', EMPTY[:-1]+b'{'*32+b'let a='+b'('*64+b'1u'+b')'*64+b';'+b'}'*32+b'}', dict(control_depth=32, expression_depth=64), INPUT_ERROR, 'depth'),
             ('combined_control33_expression64', EMPTY[:-1]+b'{'*33+b'let a='+b'('*63+b'1u'+b')'*63+b';'+b'}'*33+b'}', dict(control_depth=32, expression_depth=64), INPUT_ERROR, 'depth'),
             ('source_above_legacy_envelope', EMPTY+b' '*66000, dict(source_bytes=len(EMPTY)+66000), OK, ''),
             ('for_three_statements_exact', EMPTY[:-1]+b'for(var i:u32=0u;i<1u;i++){ } }', dict(statements=3), OK, ''),
             ('for_three_statements_short', EMPTY[:-1]+b'for(var i:u32=0u;i<1u;i++){ } }', dict(statements=2), INPUT_ERROR, 'statement')]
    for name, source, changes, expected, budget in tests:
        api = JobApi(library, capacities(**changes), extra_workspace=8192)
        status, result, words, errors = api.call(source)
        api.close()
        require(not errors, f'{name}: {errors}')
        require(status == expected, f'{name}: status {status}, expected {expected}: {diagnostic(result)}')
        if budget:
            message = bytes(result.diagnostic.message).decode().lower()
            require(budget in message, f'{name}: missing named {budget} budget: {message}')
        if expected == OUTPUT_TOO_SMALL:
            require(result.diagnostic.offset == len(source), f'{name}: word budget not source end')
        rows.append(dict(name=name, status=status, diagnostic=diagnostic(result), word_count=len(words)))
    api = JobApi(library)
    status, baseline, words, errors = api.call(STORE)
    require(status == 0 and not errors, 'word-budget baseline failed')
    count = len(words)
    for name, staging, caller, expected in [('staging_exact', count, count, OK), ('staging_short', count-1, count+10, OUTPUT_TOO_SMALL), ('caller_short', count+10, count-1, OUTPUT_TOO_SMALL)]:
        tight = JobApi(library, capacities(words=staging), extra_workspace=8192)
        status, result, output, errors = tight.call(STORE, caller)
        require(status == expected and not errors, f'{name}: {status} {errors}')
        require(status != OK or output == words, f'{name}: output mismatch')
        if status:
            require(result.diagnostic.offset == len(STORE), f'{name}: wrong word exhaustion offset')
        tight.close()
        rows.append(dict(name=name, status=status, words=count))
    # A zero-length NULL source and NULL zero-capacity output are valid spans.
    result = Result()
    status = api.compile(ABI, C.byref(api.caps), C.sizeof(api.caps), api.workspace, api.workspace_size,
                         None, 0, None, 0, C.byref(result), C.sizeof(result))
    require(status in (INPUT_ERROR, PARSE_ERROR, OUTPUT_TOO_SMALL) and not result.word_count, 'zero-length null source/words invalid-span failure')
    api.close()
    return rows


def lifecycle_checks(library: Path) -> list[dict]:
    def worker(seed: int) -> tuple:
        api = JobApi(library)
        baseline = None
        for iteration in range(12):
            source = STORE if iteration % 3 else STORE.replace(b'=1u', b'=missing')
            status, result, words, errors = api.call(source)
            require(not errors, f'concurrent guards {seed}: {errors}')
            if iteration % 3:
                require(status == OK, f'reuse success {seed}: {diagnostic(result)}')
                if baseline is None:
                    baseline = words
                require(words == baseline, f'non-deterministic workspace reuse {seed}')
            else:
                require(status == SEMANTIC_ERROR, f'reuse failure status {seed}: {status}')
        reflection = bytes(result.reflection)
        C.memset(api.workspace, 0xCE, api.workspace_size)
        require(bytes(result.reflection) == reflection, 'result retains workspace data')
        api.close()
        return seed, hashlib.sha256(bytes((U32 * len(baseline))(*baseline))).hexdigest()
    with ThreadPoolExecutor(max_workers=6) as executor:
        rows = list(executor.map(worker, range(12)))
    require(len({row[1] for row in rows}) == 1, 'concurrent words differ')
    return [dict(worker=seed, word_sha256=digest) for seed, digest in rows]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = dict(status='FAIL', checks={})
    try:
        api = JobApi(args.library)
        report['workspace'] = dict(size=api.req.size_bytes, alignment=api.req.alignment_bytes)
        for name, action in [('query', lambda: query_checks(api)), ('preflight', lambda: preflight_checks(api)),
                             ('budgets', lambda: budget_checks(args.library)), ('lifecycle', lambda: lifecycle_checks(args.library))]:
            report['checks'][name] = action()
            (args.out/'job-api.json').write_text(json.dumps(report, indent=2)+'\n')
        api.close()
        report['status'] = 'PASS'
    except Exception as error:
        report['first_failure'] = str(error)
        print('FAIL:', error)
    (args.out/'job-api.json').write_text(json.dumps(report, indent=2)+'\n')
    return 0 if report['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
