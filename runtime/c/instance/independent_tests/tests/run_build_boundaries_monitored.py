#!/usr/bin/env python3
"""Check frozen build rejection cases without executing an incompatible artifact."""
from pathlib import Path
import json
import os
import subprocess
import time
from monitored_build import run_build

base = Path('/tmp/doe-instance-evaluator-0eyngbi0')
evidence = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
receipts = evidence / 'native-build-boundaries'
receipts.mkdir(exist_ok=False)
work = base / 'native-controls-complete'
runtime = work / 'source/runtime/zig'
zig = '/workspace/shared/doe-language-experiment-20261023/phase1/deps/zig-x86_64-linux-0.15.2/zig'
prefix = '/workspace/shared/doe-language-experiment-20261023/phase1/system-link-prefix'
i = '-Dexperimental-c-instance-archive=' + str(base / 'instance-normal/libdoe_instance.a')
c = '-Dexperimental-c-callbacks-archive=' + str(base / 'callbacks-developer/libdoe_callbacks.a')
f = '-Dexperimental-c-futures-archive=' + str(base / 'accepted-futures/libdoe_futures_zig.a')
all_flags = [i, c, f]
cases = [
    ('instance-only', 'dropin', [i], 'requires both selected C futures and callbacks'),
    ('instance-future-no-callback', 'dropin', [i, f], 'requires both selected C futures and callbacks'),
    ('callback-no-future', 'dropin', [c], 'requires the selected C futures archive'),
    ('instance-relative', 'dropin', ['-Dexperimental-c-instance-archive=relative.a', c, f], 'requires an existing absolute external GCC archive'),
    ('instance-missing', 'dropin', ['-Dexperimental-c-instance-archive=/tmp/independent-instance-intentionally-absent.a', c, f], 'requires an existing absolute external GCC archive'),
    ('callback-missing', 'dropin', [i, '-Dexperimental-c-callbacks-archive=/tmp/independent-callback-intentionally-absent.a', f], 'experimental C callbacks requires an existing absolute'),
    ('future-relative', 'dropin', [i, c, '-Dexperimental-c-futures-archive=relative.a'], 'experimental C futures requires an existing absolute'),
    ('non-native-target', 'dropin', all_flags + ['-Dtarget=aarch64-linux'], 'requires native Linux x86-64'),
    ('browser-wasm', 'browser-wasm', all_flags, 'not supported by the non-native browser-wasm'),
    ('generic-native-suite', 'test', all_flags, 'retained generic native suites require the default provider'),
    ('old-future-suite', 'test-c-futures-integration', all_flags, 'old selected fixtures require retained instance'),
    ('old-callback-suite', 'test-c-callbacks-integration', all_flags, 'with retained instance; use test-c-instance-integration'),
    ('instance-suite-unselected', 'test-c-instance-integration', [], 'requires explicit instance, callback and future GCC archives'),
]
environment = os.environ.copy()
environment['DOE_BACKEND'] = 'metal'
for label, step, flags, diagnostic in cases:
    argv = [zig, 'build', step, '-j1', '-Doptimize=ReleaseFast', *flags, '--search-prefix', prefix,
            '--cache-dir', str(base / 'native-fci-retry1/cache'), '--global-cache-dir', str(base / 'native-fci-retry1/global-cache'), '--prefix', str(work / 'rejected-install')]
    start = time.monotonic()
    monitored = run_build('monitored-' + label, argv, runtime, environment, receipts, timeout=120)
    assert not monitored['timed_out'], monitored
    result = subprocess.CompletedProcess(argv, monitored['returncode'], monitored['stdout'], monitored['stderr'])
    row = {'argv': argv, 'cwd': str(runtime), 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr,
           'elapsed_seconds': time.monotonic() - start, 'required_diagnostic': diagnostic}
    (receipts / (label + '.json')).write_text(json.dumps(row, indent=2) + '\n')
    print(label, result.returncode, flush=True)
    assert result.returncode != 0 and diagnostic in result.stderr + result.stdout, row
(receipts / 'summary.json').write_text(json.dumps({'passed': True, 'explicit_rejections': len(cases), 'rejected_artifacts_executed': False}, indent=2) + '\n')
