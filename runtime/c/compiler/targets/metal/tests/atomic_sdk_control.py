#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile freshly emitted atomic MSL with Apple's actual SDK; never execute GPU work."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def main():
    out = Path(sys.argv[1]).resolve()
    compiler = subprocess.run(['xcrun', '--find', 'metal'], check=True,
                              capture_output=True, text=True, timeout=10).stdout.strip()
    version = subprocess.run([compiler, '--version'], check=True,
                             capture_output=True, text=True, timeout=10)
    sdk = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], check=True,
                         capture_output=True, text=True, timeout=10).stdout.strip()
    cases = ['signed_storage', 'unsigned_storage', 'signed_shared', 'unsigned_shared', 'effects',
             'dense_effects', 'selected_shared', 'alternate_shared']
    results = []
    for case in cases:
        source = out / ('atomic_' + case + '.metal')
        output = source.with_suffix('.air')
        command = [compiler, '-std=metal3.0', '-mmacosx-version-min=13.0',
                   '-c', str(source), '-o', str(output)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        source.with_suffix('.sdk.stdout').write_text(result.stdout)
        source.with_suffix('.sdk.stderr').write_text(result.stderr)
        results.append({'case': case, 'command': command, 'returncode': result.returncode,
                        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                        'output_sha256': hashlib.sha256(output.read_bytes()).hexdigest()
                        if result.returncode == 0 else None})
        (out / 'ATOMIC_SDK.json').write_text(json.dumps({'metal': compiler,
            'version': version.stdout + version.stderr, 'sdk': sdk, 'scope': 'compile only; no device run',
            'results': results}, indent=2) + '\n')
        if result.returncode:
            sys.stderr.write(result.stderr)
            raise SystemExit(result.returncode)
    print('PASS actual Apple SDK metal3 atomic compilation; no device execution')


if __name__ == '__main__':
    main()
