#!/usr/bin/env python3
"""Run the fixed native fixture with GCC-instrumented C owners only."""
from pathlib import Path
import json
import os
import subprocess
import time
from monitored_build import run_build

base = Path('/tmp/doe-instance-evaluator-0eyngbi0')
evidence = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
receipts = evidence / 'native-sanitizers'
receipts.mkdir(exist_ok=False)
original = json.loads((evidence / 'native-independent/native-focused-fixtures-repair1.json').read_text())
normal = [base / 'instance-normal/libdoe_instance.a', base / 'callbacks-developer/libdoe_callbacks.a', base / 'accepted-futures/libdoe_futures_zig.a']
results = []
for mode, libraries in [('asan', ['asan', 'ubsan']), ('tsan', ['tsan'])]:
    root = base / 'native-sanitizers' / mode
    replacements = [root / 'instance/libdoe_instance.a', root / 'callbacks/libdoe_callbacks.a', root / 'futures/libdoe_futures_zig.a']
    command = original['argv'].copy()
    for index, arg in enumerate(command):
        for old, new in zip(normal, replacements):
            if arg == str(old): command[index] = str(new)
        if arg == '--cache-dir': command[index + 1] = str(root / 'zig-cache')
        if arg == '--global-cache-dir': command[index + 1] = str(base / 'native-independent/global-cache')
        if arg == '--name': command[index + 1] = 'independent_instance_' + mode
        if arg.startswith('-femit-bin='): command[index] = '-femit-bin=' + str(root / 'independent-instance')
    for library in libraries:
        path = Path(subprocess.check_output(['gcc', '-print-file-name=lib' + library + '.so'], text=True).strip()).resolve()
        assert path.is_file()
        command += ['-L', str(path.parent), '-l' + library]
    environment = os.environ.copy()
    environment.update(DOE_BACKEND='metal', ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1', TSAN_OPTIONS='halt_on_error=1')
    start = time.monotonic()
    monitored = run_build('monitored-' + mode, command, Path(original['cwd']), environment, receipts)
    assert not monitored['timed_out'], monitored
    result = subprocess.CompletedProcess(command, monitored['returncode'], monitored['stdout'], monitored['stderr'])
    row = {'argv': command, 'cwd': original['cwd'], 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
           'elapsed_seconds': time.monotonic() - start, 'environment': {k: environment[k] for k in ['DOE_BACKEND', 'ASAN_OPTIONS', 'UBSAN_OPTIONS', 'TSAN_OPTIONS']},
           'instrumented_objects': ['instance.c', 'wait.c', 'capabilities.c', 'callbacks.c', 'futures.c', 'storage.c', 'doe_futures_zig_bridge.c'],
           'NOT_instrumented': ['retained native Zig consumers and fixtures', 'Metal/D3D12 C stubs', 'system libraries'],
           'leak_sanitizer': False}
    (receipts / (mode + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    results.append(row)
    print(mode, result.returncode, flush=True)
    assert result.returncode == 0, row
    assert 'All 12 tests passed.' in result.stderr + result.stdout, row
(receipts / 'summary.json').write_text(json.dumps({'passed': True, 'modes': len(results), 'tests_each': 12, 'retained_zig_instrumented': False}, indent=2) + '\n')
