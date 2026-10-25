#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Windows CLI host contracts, Unicode paths and source-bound normalization.

This invokes no execute command. It is separate from Linux loader interposition;
it proves host outputs on Windows, not a Windows no-device interception boundary.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess

from test_cli_host_modes import REPO, check_normalized


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--provider', choices=['native', 'dawn'], default='native')
    args = parser.parse_args()
    work = args.work / 'Unicode-\u03bb-\u96ea'
    work.mkdir(parents=True, exist_ok=True)
    check_normalized(args.binary, work, os.environ.copy())
    rows = work / 'host.jsonl'
    meta = work / 'host.meta.json'
    command = [str(args.binary), '--trace-jsonl', str(rows), '--trace-meta', str(meta)]
    result = subprocess.run(command, cwd=REPO, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
    contents = rows.read_bytes()
    assert b'\r' not in contents, 'artifact bytes must retain LF newlines'
    parsed = [json.loads(line) for line in contents.splitlines()]
    assert len(parsed) == 5
    metadata = json.loads(meta.read_text(encoding='utf-8'))
    assert metadata['executionRowCount'] == 0 and metadata['adapterInfo'] is None
    assert metadata['profile']['api'] == 'd3d12'
    result = subprocess.run([*command, '--replay', str(rows)], cwd=REPO, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
    malformed = work / 'mismatch.jsonl'
    parsed[0]['hash'] = '0'
    malformed.write_text(''.join(json.dumps(row) + '\n' for row in parsed), encoding='utf-8')
    result = subprocess.run([str(args.binary), '--replay', str(malformed)], cwd=REPO, capture_output=True, timeout=10)
    assert result.returncode == 1, result.stderr
    for lane in ['metal_dawn_release', 'vulkan_dawn_release', 'd3d12_dawn_release']:
        result = subprocess.run([str(args.binary), '--backend-lane', lane], cwd=REPO, capture_output=True, timeout=10)
        expected = 0 if args.provider == 'dawn' and lane == 'd3d12_dawn_release' else 2
        assert result.returncode == expected, 'provider lane mismatch: ' + lane
    assert metadata['module'] == ('dawn-c-runtime' if args.provider == 'dawn' else 'doe-c-runtime')
    print('PASS actual Windows CLI host normalization, Unicode argv/artifacts, D3D12 profile and replay/provider refusal')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
