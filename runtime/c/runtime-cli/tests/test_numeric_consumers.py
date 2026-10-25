#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Bounded unchanged WGSL consumers through the actual native provider.

Requires shader-f16. A software adapter pass is not physical GPU qualification.
Fixtures are independent exact small values, not replacements for sealed oracles.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
from test_cli import run, check


def words(values: list[float]) -> list[int]:
    return list(struct.unpack('<' + 'I' * len(values), struct.pack('<' + 'f' * len(values), *values)))


def write(handle: int, values: list[int]) -> dict:
    return {'kind': 'buffer_write', 'handle': handle, 'bufferSize': len(values) * 4, 'data': values}


def binding(index: int, handle: int, size: int, kind: str) -> dict:
    return {'binding': index, 'group': 0, 'kind': 'buffer', 'buffer_type': kind,
            'resource_handle': handle, 'buffer_size': size}


def dispatch(kernel: str, operation: str, phase: str, bindings: list[dict], x: int = 1) -> dict:
    return {'kind': 'kernel_dispatch', 'kernel': 'bench/inference-pipeline/kernels/' + kernel,
            'x': x, 'semanticOpId': operation, 'semanticStage': 'native_numeric_consumer',
            'semanticPhase': phase, 'bindings': bindings}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    norm = [write(10, [4, 0, 0, 0]), write(11, words([1] * 4)), write(12, words([2, 1, 1, 1])),
            write(20, [2, 4, 0, 0]), write(21, words([1, 0, 0, 0, 0, 1, 0, 0])),
            dispatch('rmsnorm.wgsl', 'rmsnorm.output', 'normalized_hidden',
                     [binding(0, 10, 16, 'uniform'), binding(1, 11, 16, 'readonly'),
                      binding(2, 12, 16, 'readonly'), binding(3, 13, 16, 'storage')]),
            dispatch('matmul_logits_forward_f16accum.wgsl', 'matmul.logits', 'logits',
                     [binding(0, 20, 16, 'uniform'), binding(1, 13, 16, 'readonly'),
                      binding(2, 21, 32, 'readonly'), binding(3, 22, 8, 'storage')], 2)]
    attention = [write(10, [8, 8, 0x3f800000, 0]), write(11, words([0] * 8)),
                 write(12, words([1] * 64)), write(13, words([2, 0] * 8)),
                 dispatch('attention_slice_forward_v2_f32.wgsl', 'attention.output', 'output_logits',
                          [binding(0, 10, 16, 'uniform'), binding(1, 11, 32, 'readonly'),
                           binding(2, 12, 256, 'readonly'), binding(3, 13, 64, 'readonly'),
                           binding(4, 14, 8, 'storage')])]
    for name, commands, handle, expected, operations in [
            ('rmsnorm', norm, 22, (2.0, 1.0), ['rmsnorm.output', 'matmul.logits']),
            ('attention', attention, 14, (2.0, 0.0), ['attention.output'])]:
        path = args.work / (name + '.commands.json')
        path.write_text(json.dumps(commands))
        output = args.work / (name + '.bin')
        code, stderr, meta, _ = run(args.binary, args.work, name, '--quirk-mode', 'off',
                                   '--gpu-timestamp-mode', 'off', '--readback-buffer', f'{handle}={output}', commands=path)
        check(code == 0 and meta['executionErrorCount'] == 0, f'{name}: {stderr}')
        check(struct.unpack('<2f', output.read_bytes()) == expected, f'{name} actual readback')
        receipt = args.work / (name + '.meta.json.numeric-stability.jsonl')
        rows = [json.loads(line) for line in receipt.read_text().splitlines()]
        check([row['semanticOpId'] for row in rows] == operations, f'{name} operation receipts')
        for row in rows:
            check(row['selectedToken']['fastMatchesReference'] and row['selectedToken']['stableMatchesReference'],
                  f'{name} exact policy reference')
        print('PASS unchanged', name, expected, meta['adapterInfo'])
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
