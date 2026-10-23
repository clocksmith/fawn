#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build sealed native fixture and four canonical controls from verified source copy."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ['repo', 'out', 'receipts', 'source-manifest', 'instance-archive', 'callback-archive', 'futures-archive', 'zig', 'loader-prefix']:
        parser.add_argument('--' + name, type=Path, required=True, help='Explicit ' + name.replace('-', ' ') + '.')
    parser.add_argument('--skip-controls', action='store_true', help='Run only focused fixture, controls remain separate.')
    parser.add_argument('--only-controls', action='store_true', help='Skip focused fixture; build canonical controls.')
    parser.add_argument('--lanes', default='Z,F,FC,FCI', help='Comma-separated explicit canonical lanes.')
    args = parser.parse_args()
    out, receipts, repo = args.out.resolve(), args.receipts.resolve(), args.repo.resolve()
    out.mkdir(parents=True, exist_ok=False)
    receipts.mkdir(parents=True, exist_ok=False)
    copied = out / 'source'
    here = Path(__file__).resolve().parent
    entries = []
    for row in args.source_manifest.read_text(encoding='utf-8').splitlines():
        digest, name = row.split(maxsplit=1)
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        source = repo / relative
        assert hashlib.sha256(source.read_bytes()).hexdigest() == digest, name
        destination = copied / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        entries.append((digest, name))
    shutil.copy2(args.source_manifest, receipts / 'source-inputs.sha256')
    runtime = copied / 'runtime/zig'
    fixtures = {'native_cases.zig': 'independent_instance.zig', 'build_options.fixture.zig': 'independent_options.zig'}
    for name, target in fixtures.items():
        shutil.copy2(here / name, runtime / target)
    archive_args = [args.instance_archive.resolve(), args.callback_archive.resolve(), args.futures_archive.resolve()]
    (receipts / 'archive-inputs.sha256').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest() + '  ' + str(x) + '\n' for x in archive_args), encoding='utf-8')
    records = []
    environment = os.environ.copy()
    environment['DOE_BACKEND'] = 'metal'

    def run(command: list[object], label: str, cwd: Path = out, expected: int = 0) -> subprocess.CompletedProcess[str]:
        argv = [str(x) for x in command]
        started = time.monotonic()
        result = subprocess.run(argv, cwd=cwd, env=environment, capture_output=True, text=True, timeout=900, check=False)
        record = {'argv': argv, 'cwd': str(cwd), 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
                  'elapsed_seconds': time.monotonic() - started, 'child_peak_rss_kib': resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
                  'DOE_BACKEND': 'metal'}
        (receipts / f'{label}.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n', encoding='utf-8')
        records.append(record)
        print(label, result.returncode, flush=True)
        assert result.returncode == expected, record
        return result

    run([args.zig, 'version'], 'zig-version')
    includes = []
    for path in ['runtime/c/instance/include', 'runtime/c/futures/include', 'runtime/c/futures/integration', 'runtime/c/callbacks/include',
                 'runtime/zig/vendor/webgpu-headers', 'runtime/zig/src/backend/d3d12', 'runtime/zig/src/backend/metal']:
        includes += ['-I', copied / path]
    if not args.only_controls:
        command = [args.zig, 'test', *archive_args, runtime / 'src/backend/d3d12/d3d12_bridge_stubs.c',
                   runtime / 'src/backend/metal/metal_bridge_stubs.c', '-lvulkan', '-OReleaseFast', *includes,
                   '--dep', 'build_options', '-Mroot=' + str(runtime / 'independent_instance.zig'),
                   '-Mbuild_options=' + str(runtime / 'independent_options.zig'), '-lc', '--test-filter', 'independent instance:',
                   '--cache-dir', out / 'fixture-cache', '--global-cache-dir', out / 'global-cache',
                   '--name', 'independent_instance', '-L', args.loader_prefix.resolve() / 'lib', '-femit-bin=' + str(out / 'independent_instance')]
        run(command, 'native-focused-fixtures')
    if not args.skip_controls:
        executables = []
        for compiler, language in [('gcc', 'c11'), ('g++', 'c++17')]:
            output = out / ('consumer-' + compiler)
            extra = [] if compiler == 'gcc' else ['-x', 'c++', '-fno-exceptions', '-fno-rtti']
            run([compiler, *extra, '-std=' + language, '-O2', '-g', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-pthread',
                 '-I', runtime / 'vendor/webgpu-headers', here / 'native_consumer.c', '-ldl', '-o', output], 'consumer-build-' + compiler)
            executables.append(output)
        legacy = out / 'accepted-feature-lookup'
        run(['gcc', '-std=c11', '-O2', '-I', runtime / 'vendor/webgpu-headers', runtime / 'tests/instance_feature_lookup.c', '-ldl', '-o', legacy], 'accepted-feature-consumer-build')
        for lane in args.lanes.split(','):
            assert lane in ['Z', 'F', 'FC', 'FCI']
            flags = []
            if lane != 'Z': flags.append('-Dexperimental-c-futures-archive=' + str(args.futures_archive.resolve()))
            if lane in ['FC', 'FCI']: flags.append('-Dexperimental-c-callbacks-archive=' + str(args.callback_archive.resolve()))
            if lane == 'FCI': flags.append('-Dexperimental-c-instance-archive=' + str(args.instance_archive.resolve()))
            command = [args.zig, 'build', 'dropin', '-Doptimize=ReleaseFast', *flags, '--search-prefix', args.loader_prefix.resolve(),
                       '--cache-dir', out / (lane + '-cache'), '--global-cache-dir', out / (lane + '-global-cache'), '--prefix', out / (lane + '-install')]
            run(command, lane + '-canonical-clean', runtime)
            library = out / (lane + '-install/lib/libwebgpu_doe.so')
            first_hash = hashlib.sha256(library.read_bytes()).hexdigest()
            for executable in executables:
                run([executable, library, lane], lane + '-' + executable.name)
            run([legacy, library], lane + '-accepted-feature-consumer')
            run(['nm', '-D', '--defined-only', library], lane + '-symbols')
            run(['ldd', library], lane + '-dependencies')
            run(command, lane + '-canonical-incremental', runtime)
            final_hash = hashlib.sha256(library.read_bytes()).hexdigest()
            assert first_hash == final_hash, lane + ' incremental artifact changed'
            options = []
            for path in (out / (lane + '-cache')).rglob('options.zig'):
                text = path.read_text(encoding='utf-8')
                if 'experimental_c_futures' in text:
                    target = receipts / (lane + '-options-' + hashlib.sha256(text.encode()).hexdigest()[:12] + '.zig')
                    shutil.copy2(path, target)
                    options.append(str(target))
            (receipts / (lane + '-artifact.json')).write_text(json.dumps({'library': str(library), 'sha256': final_hash,
                'size_bytes': library.stat().st_size, 'options': options}, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    for digest, name in entries:
        assert hashlib.sha256((copied / name).read_bytes()).hexdigest() == digest, 'copied source changed: ' + name
    (receipts / 'summary.json').write_text(json.dumps({'passed': True, 'verified_sources': len(entries), 'commands': len(records),
        'source_copy': str(copied), 'only_native_cpu_scope': True, 'skip_controls': args.skip_controls, 'only_controls': args.only_controls}, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
