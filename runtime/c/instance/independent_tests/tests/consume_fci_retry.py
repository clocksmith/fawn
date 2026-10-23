#!/usr/bin/env python3
"""Continue exact canonical controls after preserved Z compiler-child termination."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import time

base = Path('/tmp/doe-instance-evaluator-0eyngbi0')
evidence = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
work = base / 'native-fci-retry1'
receipts = evidence / 'native-fci-retry1'
runtime = work / 'source/runtime/zig'
zig = '/workspace/shared/doe-language-experiment-20261023/phase1/deps/zig-x86_64-linux-0.15.2/zig'
prefix = '/workspace/shared/doe-language-experiment-20261023/phase1/system-link-prefix'
archives = [base / 'instance-normal/libdoe_instance.a', base / 'callbacks-developer/libdoe_callbacks.a', base / 'accepted-futures/libdoe_futures_zig.a']
environment = os.environ.copy()
environment.update(DOE_BACKEND='metal', LD_LIBRARY_PATH=prefix + '/lib')
records = []

def run(label, command, cwd=runtime, trace=None):
    argv = [str(x) for x in command]
    env = environment.copy()
    if trace:
        trace.touch(exist_ok=False)
        env['DOE_PROGRAM_IDENTITY_TRACE_PATH'] = str(trace)
    start = time.monotonic()
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=900)
    row = {'argv': argv, 'cwd': str(cwd), 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
           'elapsed_seconds': time.monotonic() - start, 'environment': {key: env[key] for key in ('DOE_BACKEND', 'LD_LIBRARY_PATH', 'DOE_PROGRAM_IDENTITY_TRACE_PATH') if key in env}}
    (receipts / (label + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    records.append(row)
    print(label, result.returncode, flush=True)
    assert result.returncode == 0, row
    return result

for lane in ['FCI']:
    flags = []
    if lane != 'Z': flags.append('-Dexperimental-c-futures-archive=' + str(archives[2]))
    if lane in ['FC', 'FCI']: flags.append('-Dexperimental-c-callbacks-archive=' + str(archives[1]))
    if lane == 'FCI': flags.append('-Dexperimental-c-instance-archive=' + str(archives[0]))
    command = [zig, 'build', 'dropin', '-j1', '-Doptimize=ReleaseFast', *flags, '--search-prefix', prefix,
        '--cache-dir', work / 'cache', '--global-cache-dir', work / 'global-cache', '--prefix', work / 'install']
    library = work / 'install/lib/libwebgpu_doe.so'
    first_hash = hashlib.sha256(library.read_bytes()).hexdigest()
    for compiler in ['gcc', 'g++']:
        trace = work / (lane + '-' + compiler + '-trace.jsonl')
        run(lane + '-consumer-' + compiler, [base / 'native-controls-complete' / ('consumer-' + compiler), library, lane], trace=trace)
        rows = [json.loads(line) for line in trace.read_text().splitlines()]
        instance_rows = [row for row in rows if 'DoeInstance' in row.get('objectType', '')]
        assert len(instance_rows) == 56
        expected_size = {'Z': 40, 'F': 176, 'FC': 176, 'FCI': 264}[lane]
        assert all(row['objectType'] == 'native.support.doe_native_object_types.DoeInstance' and row['objectSize'] == expected_size for row in instance_rows)
        assert all(row['schemaVersion'] == 1 and row['traceKind'] == 'doe_native_program_identity_v1' for row in rows)
        assert all(a['sequence'] < b['sequence'] for a, b in zip(rows, rows[1:]))
    run(lane + '-original-features', [base / 'native-controls-complete/accepted-feature-lookup', library])
    run(lane + '-labels', [base / 'native-prebuilt/labels', library])
    symbols = run(lane + '-symbols', ['nm', '-D', '--defined-only', library]).stdout
    names = {line.split()[-1] for line in symbols.splitlines()}
    for name, present in {'doe_future_new_id': lane != 'Z', 'doe_callback_publish': lane in ['FC', 'FCI'], 'doe_instance_create': lane == 'FCI'}.items():
        assert (name in names) == present, (lane, name)
    run(lane + '-dependencies', ['ldd', library])
    run(lane + '-canonical-incremental', command)
    assert first_hash == hashlib.sha256(library.read_bytes()).hexdigest()
    options = []
    for path in (work / 'cache').rglob('options.zig'):
        text = path.read_text()
        if 'experimental_c_futures' in text:
            target = receipts / (lane + '-options-' + hashlib.sha256(text.encode()).hexdigest()[:12] + '.zig')
            shutil.copy2(path, target)
            for flag, selected in {'futures': lane != 'Z', 'callbacks': lane in ['FC', 'FCI'], 'instance': lane == 'FCI'}.items():
                assert f'pub const experimental_c_{flag}: bool = {str(selected).lower()};' in text
            options.append(str(target))
    assert options
    (receipts / (lane + '-artifact.json')).write_text(json.dumps({'library': str(library), 'sha256': first_hash, 'size_bytes': library.stat().st_size, 'options': options}, indent=2) + '\n')
for line in (evidence / 'NATIVE_COMPLETE_INPUTS.sha256').read_text().splitlines():
    digest, name = line.split(maxsplit=1)
    assert hashlib.sha256((work / 'source' / name).read_bytes()).hexdigest() == digest, name
(receipts / 'summary.json').write_text(json.dumps({'passed': True, 'commands_in_continuation': len(records), 'verified_sources': 1719,
    'source_copy': str(work / 'source'), 'FCI_first_build': '900-second timeout preserved; distinct supervised fresh retry required',
    'normal_independent_GCC_archives': True, 'only_native_cpu_scope': True}, indent=2) + '\n')
