#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Trace-only defaults and replay preflight through the actual CLI; no device calls."""
import argparse
from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parents[4]


def check_normalized(binary, work, env):
    retained = json.loads(Path(__file__).with_name('normalized_retained.json').read_text())
    for path, digest in retained['sourceHashes'].items():
        assert hashlib.sha256((REPO / path).read_bytes()).hexdigest() == digest, path + ' retained source drift'
    commands = work / 'normalized.commands.json'
    commands.write_text(json.dumps(retained['commands']))
    rows = work / 'normalized.untouched.jsonl'
    meta = work / 'normalized.untouched.meta.json'
    rows.write_text('row sentinel')
    meta.write_text('meta sentinel')
    flags = ['--emit-normalized', '--commands', str(commands), '--trace-jsonl', str(rows), '--trace-meta', str(meta)]
    # Normalization is host-only even when execute is requested, and creates no trace artifacts.
    result = subprocess.run([str(binary), *flags, '--execute', '--backend', 'native', '--command-repeat', '2'],
                            cwd=REPO, env=env, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stderr
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    expected = retained['expected'] + [{**row, 'seq': row['seq'] + len(retained['expected'])} for row in retained['expected']]
    assert actual == expected, [(a, b) for a, b in zip(actual, expected) if a != b][:1]
    assert rows.read_text() == 'row sentinel' and meta.read_text() == 'meta sentinel'
    for extra in [ ['--validate-output-oracles'], ['--readback-buffer', '1=unused'], ['--quirks', str(work/'missing.quirks')] ]:
        result = subprocess.run([str(binary), *flags, *extra], cwd=REPO, env=env, capture_output=True, text=True, timeout=10)
        assert result.returncode == 2 and not result.stdout, (extra, result.returncode, result.stderr)
    kernel = 'quoted"kernel\\name\n.wgsl'
    commands.write_text(json.dumps([dict(kind='kernel_dispatch', kernel=kernel, x=7, y=3, z=2)]))
    result = subprocess.run([str(binary), '--emit-normalized', '--commands', str(commands)],
                            cwd=REPO, env=env, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0 and json.loads(result.stdout)['kernel'] == kernel


def check_plan_dry_run(binary: Path, work: Path, env: dict[str, str]) -> None:
    from test_cli import check_schemas, plan_document, ZIG_PLAN_HASHES
    # Deliberately missing assets and kernels: the retained plan executor reads
    # them only during execution, not while emitting dry_run identities.
    env = dict(env, DOE_BENCH_ASSET_CACHE_DIR=str(work / 'never-created-assets'))
    path = work / 'dry.plan.json'
    rows_path, meta_path = work / 'dry.jsonl', work / 'dry.meta.json'
    flags = ['--plan', str(path), '--workload', 'c_plan_parity', '--trace-jsonl', str(rows_path),
             '--trace-meta', str(meta_path), '--kernel-root', str(work / 'never-created-kernels')]

    def run(document: dict[str, object], *extra: str, expected: int = 0,
            kernel_root: bool = True) -> subprocess.CompletedProcess[str] | list[dict[str, object]]:
        path.write_text(json.dumps(document), encoding="utf-8")
        rows_path.write_text('rows untouched', encoding='utf-8')
        meta_path.write_text('meta untouched', encoding='utf-8')
        selected_flags = flags if kernel_root else flags[:-2]
        result = subprocess.run([str(binary), *selected_flags, *extra], cwd=REPO, env=env,
                                capture_output=True, text=True, timeout=10)
        assert result.returncode == expected, (extra, result.returncode, result.stderr)
        if expected:
            assert rows_path.read_text(encoding="utf-8") == 'rows untouched' and meta_path.read_text(encoding="utf-8") == 'meta untouched'
            return result
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        rows = [json.loads(line) for line in rows_path.read_text(encoding="utf-8").splitlines()]
        check_schemas('plan dry run', meta, rows)
        assert meta['adapterInfo'] is None and meta['hostExecutorInitTotalNs'] == 0
        assert 'fallbackUsed' not in meta and 'backendSelectionReason' not in meta
        assert meta['executionRowCount'] == meta['executionSuccessCount'] == len(rows)
        assert meta['executionDispatchCount'] == document['dispatchCount']
        for key in ['executionErrorCount', 'executionSubmitCount', 'executionTotalNs', 'executionSetupTotalNs',
                    'executionEncodeTotalNs', 'executionSubmitWaitTotalNs', 'executionGpuTimestampTotalNs',
                    'executionGpuTimestampAttemptedCount', 'executionGpuTimestampValidCount', 'hostKernelPrewarmTotalNs']:
            assert meta[key] == 0, (key, meta[key])
        assert meta['hostPlanArtifactHash'] == document['planSha256']
        for row in rows:
            assert row['executionStatus'] == 'ok' and row['executionStatusCode'] == 'dry_run', row
            assert row['executionDispatchCount'] == (row['opCode'] == 'kernel_dispatch')
            assert row['executionSubmitCount'] == row['executionDurationNs'] == 0
        # Plan rows retain their distinct opCode layout; the command-stream
        # replay CLI intentionally does not admit this plan artifact format.
        return rows

    plan = plan_document()
    assert [row['hash'] for row in run(plan, '--dry-run')] == ZIG_PLAN_HASHES
    assert [row['hash'] for row in run(plan, '--dry-run', kernel_root=False)] == ZIG_PLAN_HASHES
    ir_path = work / 'dry.ir.json'
    ir_path.write_text(json.dumps({'shared': {'kernelRoot': str(work / 'missing-ir-kernels')}}), encoding='utf-8')
    ir_plan = {**plan, 'irPath': str(ir_path)}
    assert [row['hash'] for row in run(ir_plan, '--dry-run', kernel_root=False)] == ZIG_PLAN_HASHES
    ir_path.write_text('invalid optional IR metadata', encoding='utf-8')
    assert [row['hash'] for row in run(ir_plan, '--dry-run', kernel_root=False)] == ZIG_PLAN_HASHES
    empty = {**plan, 'commands': [], 'commandCount': 0, 'bufferWriteCount': 0, 'bufferLoadCount': 0, 'dispatchCount': 0}
    assert run(empty, '--dry-run') == []
    for extra in [ ['--execute'], ['--readback-buffer', '7001=unused'], ['--commands', 'unused'],
                   ['--workload', 'not-the-plan'], ['--trace'], ['--emit-normalized'] ]:
        run(plan, '--dry-run', *extra, expected=2)
    for mutation in [dict(dispatchCount=3), dict(commands=[None]), dict(extra=1)]:
        run({**plan, **mutation}, '--dry-run', expected=2)
    malformed = deepcopy(plan)
    malformed['commands'][1]['byteLength'] = 3
    run(malformed, '--dry-run', expected=2)
    # Normal plan execution still requires its assets rather than silently
    # executing empty loads from the dry-run lowering route.
    assert 'cannot be read' in run(plan, expected=2).stderr
    result = subprocess.run([str(binary), '--dry-run'], cwd=REPO, env=env,
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 2 and 'needs --plan' in result.stderr


def main():
    from test_cli import check_schemas, replay_passes
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True, help='Actual runtime executable')
    parser.add_argument('--no-device', type=Path, required=True, help='Instance-boundary interposer')
    parser.add_argument('--work', type=Path, required=True, help='Artifact directory')
    args = parser.parse_args(); args.work.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, LD_PRELOAD=str(args.no_device.resolve()))
    check_normalized(args.binary, args.work, env)
    check_plan_dry_run(args.binary, args.work, env)
    bare = subprocess.run([str(args.binary)], cwd=REPO, env=env, capture_output=True, text=True, timeout=10)
    assert bare.returncode == 0 and bare.stdout.count('cmd[') == 5 and 'exec backend=' not in bare.stdout
    assert 'matched=vulkan.intel.gen12.use_temp_buffer_compressed_copy' in bare.stdout
    assert len([json.loads(line[5:]) for line in bare.stdout.splitlines() if line.startswith('  -> ')]) == 5
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
    # Replay is allowed to overwrite its own trace after validation. A failed
    # preflight must preserve both files, including an alternate path spelling.
    run('embedded', '--replay', str(args.work / './embedded.jsonl'))
    saved_rows = (args.work / 'embedded.jsonl').read_bytes()
    saved_meta = (args.work / 'embedded.meta.json').read_bytes()
    changed = deepcopy(rows); changed[0]['hash'] = '0'
    (args.work / 'embedded.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in changed))
    invalid_rows = (args.work / 'embedded.jsonl').read_bytes()
    run('embedded', '--replay', str(args.work / 'embedded.jsonl'), '--execute', '--backend', 'native', expected=1)
    assert (args.work / 'embedded.jsonl').read_bytes() == invalid_rows
    assert (args.work / 'embedded.meta.json').read_bytes() == saved_meta
    (args.work / 'embedded.jsonl').write_bytes(saved_rows)
    replay = args.work / 'embedded.jsonl'
    run('replay', '--replay', str(replay))
    run('twice', '--command-repeat', '2', '--backend', 'trace')
    run('off', '--quirk-mode', 'off')
    meta, _ = run('metal-profile', '--api', 'MeTaL')
    assert meta['profile']['api'] == 'metal'
    run('bad-api', '--api', 'made-up', expected=2)
    # Native-provider lanes are validated before either tracing or device work.
    # This is also the unchanged Windows host contract's provider refusal gate.
    for lane in ['metal_dawn_release', 'vulkan_dawn_release', 'd3d12_dawn_release',
                 'METAL-DAWN-RELEASE', 'missing']:
        for mode, flags in [('trace', []), ('execute', ['--execute']),
                            ('normalized', ['--emit-normalized'])]:
            run('refuse-'+lane+'-'+mode, '--backend-lane', lane, *flags, expected=2)
    for lane in ['metal_doe_release', 'vulkan_doe_release', 'd3d12_doe_release']:
        meta, _ = run('native-lane-'+lane, '--backend-lane', lane)
        assert meta['backendLane'] == lane
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
    print('PASS host traces, retained plan dry-run hashes, 40 retained normalized commands, semantic artifacts and replay refusal before native instance creation')


if __name__ == '__main__':
    main()
