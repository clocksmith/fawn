"""Run source-only independent milestone acceptance with installed tools only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root', type=Path, required=True, help='Doe repository containing unchanged real residual sources.')
    parser.add_argument('--compiler-root', type=Path, required=True, help='Standalone compiler directory containing src/ and include/.')
    parser.add_argument('--library', type=Path, required=True, help='Shared library artifact under evaluation.')
    parser.add_argument('--cli', type=Path, required=True, help='CLI artifact under evaluation.')
    parser.add_argument('--validator', type=Path, required=True, help='Existing approved spirv-val binary. No tools are installed.')
    parser.add_argument('--out', type=Path, required=True, help='New evidence directory, must not exist.')
    args = parser.parse_args()
    for key in ['repo_root', 'compiler_root', 'library', 'cli', 'validator', 'out']:
        setattr(args, key, getattr(args, key).resolve())
    args.out.mkdir(parents=True, exist_ok=False)
    commands = []

    def run(name: str, command: list[str], environment: dict | None = None) -> None:
        result = subprocess.run(command, capture_output=True, text=True, env=environment)
        (args.out / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        commands.append(dict(name=name, command=command, returncode=result.returncode))
        (args.out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n', encoding='utf-8')
        print(name + ': ' + ('PASS' if result.returncode == 0 else 'FAIL'), flush=True)
        if result.returncode:
            print(result.stdout + result.stderr, flush=True)
            raise SystemExit(result.returncode)

    frozen = {
        args.compiler_root / 'MILESTONE_1.md': '43068d669ee01f4fe269e458a335adf7e6121b09a44fe13312aceff4a58fc6b5',
        args.compiler_root / 'include/doe_compiler.h': '5bf4f5e5a4e4624c094320ad9ec97119278bc7f6f06b0e47c01e444d42c6dc5b',
    }
    for path, expected_hash in frozen.items():
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash:
            raise SystemExit('Frozen contract/header changed: ' + str(path))
    hashes = {}
    for path in [args.library, args.cli, args.validator, args.compiler_root / 'MILESTONE_1.md', args.compiler_root / 'include/doe_compiler.h']:
        hashes[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
    (args.out / 'artifact-hashes.json').write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    run('abi-semantics', [sys.executable, str(HERE / 'evaluate.py'), '--root', str(args.repo_root),
        '--library', str(args.library), '--validator', str(args.validator), '--out', str(args.out / 'api')])
    run('cli', [sys.executable, str(HERE / 'cli_checks.py'), '--root', str(args.repo_root),
        '--cli', str(args.cli), '--validator', str(args.validator), '--out', str(args.out / 'cli')])
    run('oracle-controls', [sys.executable, str(HERE / 'test_oracle.py'), '--module', str(args.out / 'api/real_residual.spv'), '--out', str(args.out / 'oracle-controls.json')])
    native = args.out / 'native'
    native.mkdir()
    include = '-I' + str(args.compiler_root / 'include')
    sources = [str(args.compiler_root / 'src' / file) for file in ['lex.c', 'parse.c', 'validate.c', 'spirv.c', 'compile.c']]
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror']
    library = native / 'libsource_consumer.so'
    run('source-build', ['gcc', *flags, '-fPIC', '-shared', *sources,
                         '-Wl,--version-script=' + str(args.compiler_root / 'src/exports.map'), '-o', str(library)])
    for cc, standard, name in [('gcc', 'c11', 'c-consumer'), ('g++', 'c++17', 'cpp-consumer')]:
        executable = native / name
        language = ['-x', 'c++'] if cc == 'g++' else []
        run(name + '-build', [cc, '-std=' + standard, '-O2', '-Wall', '-Wextra', '-Werror', include,
            *language, str(HERE / 'native/consumer.c'), '-L' + str(native), '-lsource_consumer',
            '-Wl,-rpath,$ORIGIN', '-o', str(executable)])
        run(name, [str(executable)])
    guard = native / 'guard_harness'
    run('guard-build', ['gcc', *flags, include, str(HERE / 'native/guard_harness.c'), '-ldl', '-o', str(guard)])
    run('guard', [str(guard), str(args.library)])
    fuzz = native / 'sanitizer_fuzz'
    run('sanitizer-build', ['gcc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', include,
        str(HERE / 'native/sanitizer_fuzz.c'), *sources, '-o', str(fuzz)])
    run('sanitizer', [str(fuzz)], dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1'))
    interposer = native / 'io_faults.so'
    run('io-fault-build', ['gcc', *flags, '-fPIC', '-shared', str(HERE / 'native/io_faults.c'), '-ldl', '-o', str(interposer)])
    run('io-faults', [sys.executable, str(HERE / 'io_fault_checks.py'), '--cli', str(args.cli),
        '--interposer', str(interposer), '--source', str(args.repo_root / 'bench/inference-pipeline/kernels/residual.wgsl'),
        '--out', str(args.out / 'io-faults')])
    run('dependencies', ['ldd', str(library)])
    run('compiler-version', ['gcc', '--version'])
    run('cpp-compiler-version', ['g++', '--version'])
    print('Independent CPU/native acceptance passed. No GPU or LeakSanitizer claim.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
