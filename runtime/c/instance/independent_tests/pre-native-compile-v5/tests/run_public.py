#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independently build and execute sealed public instance fixtures, retaining outcomes."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess
import time

API_WRAPS = ['doe_future_queue_requirements', 'doe_future_queue_init', 'doe_future_queue_deinit',
             'doe_future_wait_any', 'doe_future_process_events', 'doe_future_mark_ready']
FAULT_WRAPS = ['pthread_mutex_init', 'pthread_condattr_init', 'pthread_condattr_setclock',
               'pthread_cond_init', 'pthread_condattr_destroy', 'pthread_mutex_destroy',
               'pthread_cond_destroy', 'pthread_mutex_unlock', 'doe_future_queue_deinit']
ORDINARY_FAULTS = ['init-mutex', 'init-attributes', 'init-clock', 'init-condition']
FATAL_FAULTS = {'init-attributes-destroy': 13, 'init-cleanup': 13, 'deinit-busy': 8,
                'deinit-cleanup': 13, 'deinit-system': 10, 'deinit-mutex': 13,
                'deinit-condition': 13, 'deinit-unlock': 13, 'pending': 8, 'undo-floor': None}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True, help='Candidate repository root.')
    parser.add_argument('--out', type=Path, required=True, help='New private output directory.')
    parser.add_argument('--receipts', type=Path, required=True, help='New compact receipt directory.')
    parser.add_argument('--instance-archive', type=Path, help='Explicit prebuilt owner, separately attributed.')
    parser.add_argument('--futures-archive', type=Path, help='Explicit prebuilt prerequisite.')
    parser.add_argument('--mode', choices=['normal', 'asan', 'tsan'], default='normal', help='Exact C instrumentation.')
    args = parser.parse_args()
    repo, out, receipts = args.repo.resolve(), args.out.resolve(), args.receipts.resolve()
    out.mkdir(parents=True, exist_ok=False)
    receipts.mkdir(parents=True, exist_ok=False)
    here = Path(__file__).resolve().parent
    records: list[dict[str, object]] = []
    environment = os.environ.copy()
    if args.mode == 'asan':
        environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')

    def run(command: list[object], label: str, expected_fatal: bool = False, status: int | None = None) -> subprocess.CompletedProcess[str]:
        argv = [str(x) for x in command]
        started = time.monotonic()
        result = subprocess.run(argv, capture_output=True, text=True, timeout=180, env=environment, check=False)
        record = {'argv': argv, 'cwd': str(Path.cwd()), 'returncode': result.returncode,
                  'stdout': result.stdout, 'stderr': result.stderr, 'elapsed_seconds': time.monotonic() - started,
                  'child_peak_rss_kib': resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                  'instrumentation': args.mode, 'asan_options': environment.get('ASAN_OPTIONS')}
        (receipts / f'{label}.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        records.append(record)
        print(label, result.returncode, flush=True)
        if expected_fatal:
            assert result.returncode == -6, record
            assert 'UNEXPECTED RETURN' not in result.stderr and 'LABEL' not in result.stdout and 'FREE' not in result.stdout, record
            assert 'instance' in result.stderr.lower(), record
            if status is not None:
                assert str(status) in result.stderr, record
        else:
            assert result.returncode == 0, record
        return result

    run(['gcc', '--version'], 'gcc-version')
    run(['g++', '--version'], 'gpp-version')
    run(['uname', '-a'], 'host')
    flags = ['-O2' if args.mode == 'normal' else '-O1', '-g', '-fPIC', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-pthread']
    if args.mode != 'normal':
        flags += ['-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-fsanitize=' + ('address,undefined' if args.mode == 'asan' else 'thread')]
    include = ['-I', repo / 'runtime/c/instance/include', '-I', repo / 'runtime/c/futures/include', '-I', repo / 'runtime/zig/vendor/webgpu-headers']
    archives: list[Path] = []
    inputs: list[Path] = list(here.glob('*.c')) + list(here.glob('*.cpp')) + [Path(__file__).resolve()]
    for owner, supplied in [('instance', args.instance_archive), ('futures', args.futures_archive)]:
        if supplied:
            archive = supplied.resolve()
            assert archive.is_file()
        else:
            sources = sorted((repo / f'runtime/c/{owner}/src').glob('*.c'))
            assert sources, owner
            objects = []
            inputs.extend(sources)
            inputs.extend(sorted((repo / f'runtime/c/{owner}').rglob('*.h')))
            for source in sources:
                obj = out / f'{owner}-{source.stem}.o'
                run(['gcc', '-std=c11', *flags, *include, '-c', source, '-o', obj], f'compile-{owner}-{source.stem}')
                objects.append(obj)
            archive = out / f'libdoe_{owner}.a'
            run(['ar', 'rcs', archive, *objects], f'archive-{owner}')
        archives.append(archive)
        run(['nm', '-g', '--defined-only', archive], f'symbols-{owner}')
        run(['ar', 't', archive], f'members-{owner}')
    inputs.extend(archives)
    (receipts / 'inputs.sha256').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest() + '  ' + str(x) + '\n' for x in inputs), encoding='utf-8')
    public = out / 'public'
    run(['gcc', '-std=c11', *flags, *include, here / 'instance_api.c', *archives, *['-Wl,--wrap=' + x for x in API_WRAPS], '-o', public], 'link-public')
    run([public], 'run-public')
    cpp = out / 'cpp'
    run(['g++', '-std=c++17', '-fno-exceptions', '-fno-rtti', *flags, *include, here / 'instance_cpp.cpp', *archives, '-o', cpp], 'link-cpp')
    run([cpp], 'run-cpp')
    if args.mode == 'normal':
        fault = out / 'faults'
        run(['gcc', '-std=c11', *flags, *include, here / 'instance_faults.c', *archives, *['-Wl,--wrap=' + x for x in FAULT_WRAPS], '-o', fault], 'link-faults')
        for name in ORDINARY_FAULTS:
            run([fault, name], name)
        for name, status in FATAL_FAULTS.items():
            run([fault, name], name, True, status)
        abi_outputs = []
        for compiler, lang in [('gcc', 'c11'), ('g++', 'c++17')]:
            abi = out / ('abi-' + compiler)
            run([compiler, '-x', 'c' if compiler == 'gcc' else 'c++', '-std=' + lang, *flags, *include, here / 'abi_probe.c', '-o', abi], 'link-abi-' + compiler)
            abi_outputs.append(run([abi], 'run-abi-' + compiler).stdout)
        assert abi_outputs[0] == abi_outputs[1], 'GCC/G++ crossing-layout disagreement'
    summary = {'passed': True, 'mode': args.mode, 'commands': len(records), 'source_built_instance': args.instance_archive is None,
               'source_built_futures': args.futures_archive is None, 'output_root': str(out), 'no_zig_instrumentation': True}
    (receipts / 'summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
