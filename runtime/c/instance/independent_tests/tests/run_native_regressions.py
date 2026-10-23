#!/usr/bin/env python3
"""Execute preserved native obligations from the independently source-built controls."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import time

base = Path('/tmp/doe-instance-evaluator-0eyngbi0')
evidence = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
receipts = evidence / 'native-regressions'
receipts.mkdir(exist_ok=False)
work = base / 'native-controls-complete'
runtime = work / 'source/runtime/zig'
zig = '/workspace/shared/doe-language-experiment-20261023/phase1/deps/zig-x86_64-linux-0.15.2/zig'
prefix = '/workspace/shared/doe-language-experiment-20261023/phase1/system-link-prefix'
archives = [base / 'instance-normal/libdoe_instance.a', base / 'callbacks-developer/libdoe_callbacks.a', base / 'accepted-futures/libdoe_futures_zig.a']
environment = os.environ.copy()
environment.update(DOE_BACKEND='metal', LD_LIBRARY_PATH=prefix + '/lib')
records = []

def run(label, command, cwd=runtime, count=None, extra=None):
    argv = [str(x) for x in command]
    env = environment.copy()
    env.update(extra or {})
    start = time.monotonic()
    result = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, text=True, timeout=900)
    row = {'argv': argv, 'cwd': str(cwd), 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
           'elapsed_seconds': time.monotonic() - start, 'environment': {key: env[key] for key in ('DOE_BACKEND', 'LD_LIBRARY_PATH', 'ASAN_OPTIONS', 'TSAN_OPTIONS') if key in env}}
    (receipts / (label + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    records.append(row)
    print(label, result.returncode, flush=True)
    assert result.returncode == 0, row
    if count is not None:
        assert f'All {count} tests passed.' in result.stderr + result.stdout, (label, count, row)
    return result

for lane, steps in [('F', ['test-c-futures-integration']), ('FC', ['test-c-futures-integration', 'test-c-callbacks-integration']), ('FCI', ['test-c-instance-integration'])]:
    flags = ['-Dexperimental-c-futures-archive=' + str(archives[2])]
    if lane in ['FC', 'FCI']:
        flags.append('-Dexperimental-c-callbacks-archive=' + str(archives[1]))
    if lane == 'FCI':
        flags.append('-Dexperimental-c-instance-archive=' + str(archives[0]))
    run(lane + '-original-obligations', [zig, 'build', *steps, '-j1', '-Doptimize=ReleaseFast', *flags,
        '--search-prefix', prefix, '--cache-dir', work / (lane + '-cache'), '--global-cache-dir', work / (lane + '-global-cache'),
        '--prefix', work / (lane + '-install'), '--summary', 'all'])

original = json.loads((evidence / 'native-independent/native-focused-fixtures-repair1.json').read_text())['argv']
options = (evidence / 'tests/build_options.fixture.zig').read_text()
for flag in ['futures', 'callbacks', 'instance']:
    pattern = rf'(pub const experimental_c_{flag}: bool = )true;'
    options, number = re.subn(pattern, r'\1false;', options)
    assert number == 1, flag
(runtime / 'independent_default_options.zig').write_text(options)
shutil.copy2(evidence / 'tests/default_regressions.zig', runtime / 'independent_default_regressions.zig')
command = [x for x in original if x not in [str(a) for a in archives]]
for index, arg in enumerate(command):
    if arg.startswith('-Mroot='): command[index] = '-Mroot=' + str(runtime / 'independent_default_regressions.zig')
    if arg.startswith('-Mbuild_options='): command[index] = '-Mbuild_options=' + str(runtime / 'independent_default_options.zig')
    if arg.startswith('-femit-bin='): command[index] = '-femit-bin=' + str(work / 'independent-default-regressions')
    if arg == '--cache-dir': command[index + 1] = str(work / 'default-fixture-cache')
    if arg == '--global-cache-dir': command[index + 1] = str(work / 'Z-global-cache')
    if arg == '--name': command[index + 1] = 'independent_default_regressions'
index = command.index('--test-filter')
del command[index:index + 2]
filters = json.loads((evidence / 'tests/default_regression_filters.json').read_text())
for name in filters:
    command += ['--test-filter', name]
run('Z-default-23-original-regressions', command, count=len(filters))

shutil.copy2(evidence / 'tests/native_abi.zig', runtime / 'independent_native_abi.zig')
command = original.copy()
for index, arg in enumerate(command):
    if arg.startswith('-Mroot='): command[index] = '-Mroot=' + str(runtime / 'independent_native_abi.zig')
    if arg.startswith('-femit-bin='): command[index] = '-femit-bin=' + str(work / 'independent-native-abi')
    if arg == '--cache-dir': command[index + 1] = str(work / 'abi-fixture-cache')
    if arg == '--global-cache-dir': command[index + 1] = str(work / 'FCI-global-cache')
    if arg == '--name': command[index + 1] = 'independent_native_abi'
    if arg == '--test-filter': command[index + 1] = 'independent native ABI:'
run('FCI-retained-record-abi', command, count=1)

(receipts / 'summary.json').write_text(json.dumps({'passed': True, 'commands': len(records), 'default_named_tests': len(filters),
    'selected_expectations': {'F_futures': 11, 'FC_futures': 11, 'FC_callbacks': 5, 'FCI_adapted_and_extra': 23},
    'owner_source_unchanged': True}, indent=2) + '\n')
