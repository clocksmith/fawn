#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real command-graph readbacks with independent nonzero host references.

Uses an explicitly selected adapter. CPU ICD runs prove no physical GPU behavior.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--adapter', choices=('software', 'physical'), default='software')
    parser.add_argument('--unknown-close-library', type=Path, help='Linux-only explicit close fault injection')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    binary = args.binary.resolve()
    source = '@group(0) @binding(0) var<storage,read> src:array<f32>;\n' \
        '@group(0) @binding(1) var<storage,read_write> dst:array<f32>;\n' \
        '@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) id:vec3<u32>)' \
        '{if(id.x<4u){dst[id.x]=src[id.x]*2.0;}}\n'
    shader = out / 'double.wgsl'
    shader.write_text(source)
    expected = struct.pack('<4f', 2, 4, 6, 8)
    reference = out / 'reference.f32le'
    reference.write_bytes(expected)
    base = [
        {'kind': 'buffer_write', 'handle': 71, 'bufferSize': 16,
         'data': list(struct.unpack('<4I', struct.pack('<4f', 1, 2, 3, 4)))},
        {'kind': 'buffer_write', 'handle': 72, 'bufferSize': 16,
         'data': [0x7fc00000] * 4},  # Poisoned: no-store must fail.
        {'kind': 'kernel_dispatch', 'kernel': str(shader), 'x': 4, 'y': 1, 'z': 1,
         'repeat': 1, 'warmup_dispatch_count': 0, 'bindings': [
             {'binding': 0, 'group': 0, 'kind': 'buffer', 'buffer_type': 'readonly',
              'resource_handle': 71, 'buffer_size': 16, 'visibility': 'compute'},
             {'binding': 1, 'group': 0, 'kind': 'buffer', 'buffer_type': 'storage',
              'resource_handle': 72, 'buffer_size': 16, 'visibility': 'compute'}]},
    ]
    reports = []

    def run(name: str, oracle: dict, code: int, *, no_store: bool = False) -> dict | None:
        folder = out / name
        folder.mkdir()
        case_shader = folder / 'double.wgsl'
        case_shader.write_text(source.replace('dst[id.x]=src[id.x]*2.0;', '') if no_store else source,
                               encoding='utf-8')
        commands = json.loads(json.dumps(base))
        commands[-1]['kernel'] = str(case_shader)
        case_oracle = dict(oracle)
        if 'reference_path' in case_oracle:
            case_reference = folder / 'reference.f32le'
            case_reference.write_bytes(Path(case_oracle['reference_path']).read_bytes())
            case_oracle['reference_path'] = str(case_reference)
        (folder / 'independent-expected.f32le').write_bytes(expected)
        commands[-1]['output_oracle'] = case_oracle
        path = folder / 'commands.json'
        path.write_text(json.dumps(commands))
        meta = folder / 'trace.meta.json'
        command = [str(binary), '--execute', '--adapter', args.adapter,
                   '--commands', str(path), '--quirk-mode', 'off', '--gpu-timestamp-mode', 'off',
                   '--webgpu-ffi-queue-wait-timeout-ns', '2000000000',
                   '--validate-output-oracles', '--trace-meta', str(meta),
                   '--trace-jsonl', str(folder / 'trace.ndjson')]
        with (folder / 'stdout.log').open('wb') as stdout, (folder / 'stderr.log').open('wb') as stderr:
            child = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=30)
        reports.append({'case': name, 'command': command, 'returnCode': child.returncode,
                        'expectedReturnCode': code, 'shaderSha256': hashlib.sha256(case_shader.read_bytes()).hexdigest()})
        (out / 'results.json').write_text(json.dumps({'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
            'icd': os.environ.get('VK_DRIVER_FILES'), 'cases': reports}, indent=2) + '\n')
        if child.returncode != code:
            raise AssertionError(f'{name} expected {code}, actual {child.returncode}: {(folder / "stderr.log").read_text()}')
        if code == 2:
            assert not meta.exists(), 'invalid reference reached device/output creation'
            return None
        result = json.loads(meta.read_bytes())
        assert result['runtimeCloseComplete'] is True and result['gpuCompletionUnknown'] is False
        assert result['fallbackUsed'] is False  # Fixed explicit provider; CPU selection is separately identified.
        assert result['adapterInfo']['backendType'] == 'vulkan'
        if args.adapter == 'software':
            assert result['adapterInfo']['adapterType'] == 'cpu'
        else:
            assert result['adapterInfo']['adapterType'] in ('integrated_gpu', 'discrete_gpu')
        return result

    common = {'scope': 'command_graph', 'reference_class': 'independent_v1',
              'initialization': 'zero_fill_v1', 'binding_group': 0, 'binding': 1,
              'dispatch_count': 1, 'reference_id': 'independent-double-f32-v1'}
    exact = dict(common, schema_version=2, kind='sha256_exact_v1',
                 expected_sha256=hashlib.sha256(expected).hexdigest())
    result = run('exact-nonzero', exact, 0)
    assert result['outputOracleMatchedCount'] == 1 and result['outputOracleFailedCount'] == 0
    assert result['outputOracleActualSha256'] == exact['expected_sha256']
    tolerance = dict(common, schema_version=3, kind='float32_reference_tolerance_v1',
                     reference_sha256=hashlib.sha256(expected).hexdigest(),
                     reference_path=str(reference), absolute_tolerance=1e-5, relative_tolerance=1e-5)
    result = run('float32-nonzero', tolerance, 0)
    assert result['outputOracleKind'] == tolerance['kind'] and result['outputOracleComparedValueCount'] == 4
    assert result['outputOracleMismatchCount'] == 0 and result['outputOracleMatchedCount'] == 1
    assert result['outputOracleReferenceClass'] == 'independent_v1'
    result = run('no-store-poison-rejected', tolerance, 1, no_store=True)
    assert result['outputOracleMatchedCount'] == 0 and result['outputOracleFailedCount'] == 1
    assert result['outputOracleMismatchCount'] == 4
    consensus = dict(tolerance, reference_class='cross_runtime_consensus_v1')
    result = run('consensus-class-preserved', consensus, 0)
    assert result['outputOracleReferenceClass'] == 'cross_runtime_consensus_v1'
    bad = dict(tolerance, reference_sha256='0' * 64)
    run('wrong-reference-hash-pre-device', bad, 2)
    reference.write_bytes(expected[:-4])
    run('short-reference-pre-device', tolerance, 2)
    if args.unknown_close_library:
        folder = out / 'unconfirmed-close-metadata'
        folder.mkdir()
        commands = folder / 'commands.json'
        commands.write_text(json.dumps(base[:1]))
        meta = folder / 'trace.meta.json'
        environment = os.environ.copy()
        environment['LD_PRELOAD'] = str(args.unknown_close_library.resolve())
        command = [str(binary), '--execute', '--adapter', args.adapter,
                   '--commands', str(commands), '--trace-meta', str(meta),
                   '--quirk-mode', 'off', '--gpu-timestamp-mode', 'off']
        with (folder / 'stdout.log').open('wb') as stdout, (folder / 'stderr.log').open('wb') as stderr:
            child = subprocess.run(command, env=environment, stdout=stdout, stderr=stderr, timeout=30)
        assert child.returncode == 1
        result = json.loads(meta.read_bytes())
        assert result['runtimeCloseComplete'] is False and result['gpuCompletionUnknown'] is True
        reports.append({'case': 'unconfirmed-close-metadata', 'returnCode': child.returncode,
                        'injectedLibrarySha256': hashlib.sha256(args.unknown_close_library.read_bytes()).hexdigest()})
    (out / 'results.json').write_text(json.dumps({
        'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'icd': os.environ.get('VK_DRIVER_FILES'), 'cases': reports,
    }, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': 'passed', 'cases': len(reports), 'adapter': args.adapter,
                      'scope': 'nonzero graph/exact/float32/no-store/class/reference preflight only'}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
