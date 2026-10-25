#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Trace-only defaults and replay preflight through the actual CLI; no device calls."""
import argparse
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess

from test_cli import REPO, check_schemas, replay_passes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True, help='Actual runtime executable')
    parser.add_argument('--no-device', type=Path, required=True, help='Instance-boundary interposer')
    parser.add_argument('--work', type=Path, required=True, help='Artifact directory')
    args = parser.parse_args(); args.work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LD_PRELOAD=str(args.no_device.resolve()))
    def run(name, *flags, expected=0):
        meta = args.work / (name + '.meta.json'); rows = args.work / (name + '.jsonl')
        result = subprocess.run([str(args.binary), '--trace-jsonl', str(rows), '--trace-meta', str(meta), *flags],
                                cwd=REPO, env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode == expected, (name, result.returncode, result.stderr)
        if expected: return result
        m = json.loads(meta.read_text()); r = [json.loads(line) for line in rows.read_text().splitlines()]
        check_schemas(name, m, r)
        assert m['executionRowCount'] == 0 and m['executionSuccessCount'] == 0 and m['adapterInfo'] is None
        assert 'executionBackend' not in m and all('executionStatus' not in row for row in r)
        assert replay_passes(args.work, name)
        return m, r
    # Fresh retained Zig CLI (source unchanged at796b98338), 2026-10-25 host-only.
    expected_hashes = ['0xd3d58a45b3411d9c','0xdf1715b3537d5cb3','0x4585478a9f8ef747','0x932455c45a37ada4','0xfe241c2a680fa89c']
    _, rows = run('embedded')
    assert [row['hash'] for row in rows] == expected_hashes
    replay = args.work / 'embedded.jsonl'
    run('replay', '--replay', str(replay))
    run('twice', '--command-repeat', '2', '--backend', 'trace')
    run('off', '--quirk-mode', 'off')
    meta, _ = run('metal-profile', '--api', 'MeTaL')
    assert meta['profile']['api'] == 'metal'
    run('bad-api', '--api', 'made-up', expected=2)
    missing = run('missing-replay', '--replay', str(args.work / 'missing.jsonl'), expected=1)
    assert 'cannot read' in missing.stderr
    for field, value in [('seq', 3), ('module', 'other-runtime'), ('opCode', 'buffer_write'), ('command', 'upload'),
                         ('hash', '0'), ('previousHash', '0'), ('kernel', 'unexpected.wgsl')]:
        changed = deepcopy(rows); changed[0][field] = value
        path = args.work / (field + '.bad.jsonl'); path.write_text(''.join(json.dumps(r)+'\n' for r in changed))
        # A bad replay must fail before the native instance boundary, even with execute requested.
        run('bad-'+field, '--replay', str(path), '--execute', '--backend', 'native', expected=1)
    for name, modified in [('missing', rows[:-1]), ('extra', rows+rows[-1:])]:
        path = args.work / (name + '.bad.jsonl'); path.write_text(''.join(json.dumps(r)+'\n' for r in modified))
        run('bad-'+name, '--replay', str(path), expected=1)
    commands = args.work / 'semantic.commands.json'
    commands.write_text(json.dumps([dict(kind='kernel_dispatch', kernel='never-compiled.wgsl', semanticOpId='matmul.logits',
                                       semanticStage='host', semanticPhase='trace', captureBufferHandle=41, captureSize=4)]))
    run('semantic', '--commands', str(commands), '--quirk-mode', 'off')
    record = json.loads((args.work/'semantic.meta.json.operators.json').read_text())[0]
    assert record['execution'] is None and record['capture']['status'] == 'unsupported'
    assert record['capture']['errorCode'] == 'capture_requires_native_execution'
    run('native-control', '--commands', str(REPO/'examples/p0_resource_lifecycle_commands.json'),
        '--execute', '--backend', 'native', '--quirk-mode', 'off', expected=99)
    print('PASS host traces, embedded source hashes, semantic artifacts and replay refusal before native instance creation')


if __name__ == '__main__':
    main()
