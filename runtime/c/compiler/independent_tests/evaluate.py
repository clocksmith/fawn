"""Independent ABI, instruction semantics, validator, and CLI acceptance."""
from __future__ import annotations

import argparse
import concurrent.futures
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import fixtures
from semantic_checks import run_vectors
from typed_oracle import Module, OracleLimit

U32 = C.c_uint32
SIZE = C.c_size_t
MAX_WORDS = 4096
SENTINEL = 0xa55a36c9


class Binding(C.Structure):
    _fields_ = [('name', C.c_char * 64), ('group', U32), ('binding', U32),
                ('scalar', C.c_int), ('access', C.c_int),
                ('element_stride', U32), ('runtime_sized', U32)]


class Reflection(C.Structure):
    _fields_ = [('entry_point', C.c_char * 64), ('workgroup_size', U32 * 3),
                ('binding_count', U32), ('bindings', Binding * 8)]


class Diagnostic(C.Structure):
    _fields_ = [('status', C.c_int), ('offset', SIZE), ('line', U32), ('column', U32),
                ('message', C.c_char * 192)]


class Result(C.Structure):
    _fields_ = [('word_count', SIZE), ('reflection', Reflection), ('diagnostic', Diagnostic)]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class Evaluator:
    def __init__(self, library: Path, output: Path, validator: Path):
        self.library = C.CDLL(str(library.resolve()))
        self.compile = self.library.doe_compiler_compile
        self.compile.argtypes = [U32, C.c_void_p, SIZE, C.POINTER(U32), SIZE,
                                 C.POINTER(Result), SIZE]
        self.compile.restype = C.c_int
        self.output, self.validator = output, validator
        self.records = []
        self.cpu_trials = 0
        output.mkdir(parents=True, exist_ok=True)

    def call(self, source: bytes, capacity: int = MAX_WORDS, version: int = 1,
             result_size: int | None = None, null_source: bool = False,
             null_words: bool = False, null_result: bool = False,
             source_length: int | None = None) -> tuple:
        data = (C.c_ubyte * max(1, len(source)))()
        C.memmove(data, source, len(source))
        words = (U32 * (MAX_WORDS + 16))(*([SENTINEL] * (MAX_WORDS + 16)))
        result_mem = (C.c_ubyte * (C.sizeof(Result) + 32))()
        C.memset(result_mem, 0xcd, len(result_mem))
        result_ptr = C.cast(C.byref(result_mem, 16), C.POINTER(Result))
        ptr = C.cast(C.byref(words, 32), C.POINTER(U32))
        before_result, before_words = bytes(result_mem), bytes(words)
        size = C.sizeof(Result) if result_size is None else result_size
        n = len(source) if source_length is None else source_length
        status = self.compile(version, None if null_source else C.cast(data, C.c_void_p), n,
            None if null_words else ptr, capacity, None if null_result else result_ptr, size)
        result = Result.from_buffer_copy(bytes(result_mem)[16:16 + C.sizeof(Result)])
        errors = []
        if bytes(data)[:len(source)] != source:
            errors.append('borrowed source mutated')
        if bytes(result_mem)[:16] != before_result[:16] or bytes(result_mem)[-16:] != before_result[-16:]:
            errors.append('result boundary canary changed')
        if list(words[:8]) != [SENTINEL] * 8 or any(w != SENTINEL for w in words[8 + min(capacity, MAX_WORDS):]):
            errors.append('output boundary canary changed')
        if null_result or version != 1 or size != C.sizeof(Result):
            if bytes(result_mem) != before_result or bytes(words) != before_words:
                errors.append('ABI/null-result rejection wrote caller buffers')
            want = 1 if null_result else 6
            if status != want:
                errors.append(f'ABI/null-result status {status} != {want}')
            return status, result, [], errors
        if status != result.diagnostic.status:
            errors.append('status/diagnostic mismatch')
        if status:
            if bytes(words) != before_words:
                errors.append('failure published partial words')
            if result.word_count or bytes(result.reflection) != b'\0' * C.sizeof(Reflection):
                errors.append('failure leaked reflection/count')
            if status not in range(1, 7):
                errors.append('undefined status value')
            diag = result.diagnostic
            if diag.offset > n or diag.line < 1 or diag.column < 1:
                errors.append('invalid diagnostic range')
            msg = C.string_at(C.addressof(diag) + Diagnostic.message.offset, 192)
            if b'\0' not in msg or len(msg.split(b'\0')[0]) < 4:
                errors.append('unhelpful/nonterminated diagnostic')
            if diag.offset <= len(source) and b'\r' not in source and not null_source:
                prefix = source[:diag.offset]
                pos = (prefix.count(b'\n') + 1, len(prefix.rsplit(b'\n', 1)[-1]) + 1)
                if (diag.line, diag.column) != pos:
                    errors.append(f'wrong original-source position {(diag.line, diag.column)} != {pos}')
        elif not (0 < result.word_count <= capacity <= MAX_WORDS):
            errors.append('invalid success word count')
        return status, result, list(words[8:8 + min(result.word_count, MAX_WORDS)]) if not status else [], errors

    def record(self, name: str, errors: list, **details) -> None:
        self.records.append(dict(case=name, passed=not errors, errors=errors, **details))
        if errors:
            print(json.dumps(self.records[-1]), flush=True)

    def valid(self, name: str, source: bytes, expected: dict) -> None:
        status, result, words, errors = self.call(source)
        details = dict(source_sha256=sha(source), status=status,
                       diagnostic=result.diagnostic.message.decode(errors='replace'))
        if status:
            errors.append('admitted source rejected')
        else:
            reflection = result.reflection
            got = dict(entry=reflection.entry_point.decode(), n=reflection.workgroup_size[0],
                bindings=[{field: getattr(b, field).decode() if field == 'name' else getattr(b, field)
                           for field, _ in Binding._fields_}
                          for b in list(reflection.bindings)[:reflection.binding_count]])
            want = {key: expected[key] for key in ['entry', 'n', 'bindings']}
            if got != want or list(reflection.workgroup_size)[1:] != [1, 1]:
                errors.append(f'reflection mismatch {got} != {want}')
            raw = struct.pack('<' + 'I' * len(words), *words)
            path = self.output / (name + '.spv')
            path.write_bytes(raw)
            details['output_sha256'] = sha(raw)
            try:
                module = Module(words)
                details['structure'] = module.interface(expected)
                details['cpu_trials'] = run_vectors(module, expected)
                self.cpu_trials += details['cpu_trials']
            except OracleLimit as exc:
                errors.append('ORACLE_UNSUPPORTED: ' + str(exc))
            except (ValueError, KeyError, IndexError, RuntimeError, AssertionError, Exception) as exc:
                errors.append(type(exc).__name__ + ': ' + str(exc))
            validation = subprocess.run([str(self.validator), '--target-env', 'vulkan1.1', str(path)],
                                        capture_output=True, text=True, timeout=10)
            details['validator_returncode'] = validation.returncode
            if validation.returncode:
                errors.append('spirv-val: ' + validation.stderr)
        self.record(name, errors, **details)

    def invalid(self, name: str, source: bytes, offset=None, category=None) -> None:
        status, result, _, errors = self.call(source)
        if not status:
            errors.append('invalid source accepted')
        if category is not None and status != category:
            errors.append(f'category {status} != {category}')
        if offset is not None and result.diagnostic.offset != offset:
            errors.append(f'diagnostic offset {result.diagnostic.offset} != {offset}')
        self.record(name, errors, status=status, offset=result.diagnostic.offset,
                    diagnostic=result.diagnostic.message.decode(errors='replace'))

    def boundary(self, source: bytes) -> None:
        _, result, words, _ = self.call(source)
        for version in [0, 2, 0xffffffff]:
            st, _, _, errors = self.call(source, version=version)
            self.record('abi_version_' + str(version), errors, status=st)
        for size in [0, 1, C.sizeof(Result) - 1, C.sizeof(Result) + 1, 2**32]:
            st, _, _, errors = self.call(source, result_size=size)
            self.record('result_size_' + str(size), errors, status=st)
        for name, kwargs, expected in [
            ('null_result', dict(null_result=True), 1),
            ('null_source_positive', dict(null_source=True), 1),
            ('null_source_empty', dict(null_source=True, source_length=0), None),
            ('null_words_positive', dict(null_words=True), 1),
            ('null_words_zero', dict(null_words=True, capacity=0), 5),
            ('size_max_input', dict(source_length=C.c_size_t(-1).value), 1)]:
            st, _, _, errors = self.call(source, **kwargs)
            if not st or (expected is not None and st != expected):
                errors.append(f'unexpected status {st} wanted {expected}')
            self.record(name, errors, status=st)
        if result.word_count:
            for capacity in range(result.word_count):
                st, _, _, errors = self.call(source, capacity=capacity)
                if st != 5:
                    errors.append('insufficient capacity did not reject OUTPUT_TOO_SMALL')
                self.record('capacity_' + str(capacity), errors, status=st)
            st, _, emitted, errors = self.call(source, capacity=result.word_count)
            if st or emitted != words:
                errors.append('exact output capacity not deterministic')
            self.record('capacity_exact', errors, status=st)

    def repeated_parallel(self, cases: list) -> None:
        selected = [cases[i] for i in [0, 1, 52, 53, 71, 72, 73, 74, len(cases) - 1]]
        baseline = {name: self.call(src) for name, src, _ in selected}
        def check(task: int) -> list:
            name, source, _ = selected[task % len(selected)]
            st, result, words, errors = self.call(source)
            old_st, old_r, old_w, old_e = baseline[name]
            if st != old_st or words != old_w or bytes(result.reflection) != bytes(old_r.reflection):
                errors.append('nondeterministic parallel output/reflection')
            return errors + old_e
        with concurrent.futures.ThreadPoolExecutor(max_workers=12) as pool:
            errors = [e for result in pool.map(check, range(1200)) for e in result]
        self.record('parallel_1200_calls_12_threads', errors)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--validator', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    evaluator = Evaluator(args.library, args.out, args.validator)
    cases = fixtures.admitted(args.root)
    for case in cases:
        evaluator.valid(*case)
    for case in fixtures.rejected():
        evaluator.invalid(*case)
    evaluator.boundary(cases[72][1])
    evaluator.repeated_parallel(cases)
    report = dict(checks=len(evaluator.records), passed=sum(r['passed'] for r in evaluator.records),
        failed=[r for r in evaluator.records if not r['passed']], cpu_trials=evaluator.cpu_trials,
        library_sha256=sha(args.library.read_bytes()), records=evaluator.records)
    (args.out / 'results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key != 'records'}, indent=2))
    return int(bool(report['failed']))


if __name__ == '__main__':
    raise SystemExit(main())
