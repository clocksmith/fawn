#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Explicit public renderer route and provider refusal through the real CLI."""
import argparse
import json
from pathlib import Path
import subprocess

from test_cli import REPO, check_schemas, replay_passes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--native', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    commands = args.work / 'commands.json'
    commands.write_text(json.dumps([
        dict(kind='render_draw', drawCount=3, targetHandle=700, targetWidth=32, targetHeight=32,
             encodeMode='render-pass', pipelineMode='redundant', bindGroupMode='redundant', bindGroupDynamicOffsets=[256]),
        dict(kind='copy_texture_to_buffer', src_handle=700, src_width=32, src_height=32, src_format='rgba8unorm',
             src_usage=19, dst_handle=701, dst_bytes_per_row=256, dst_rows_per_image=32, bytes=8192)]))
    rows = args.work / 'public.jsonl'
    meta = args.work / 'public.meta.json'
    pixels = args.work / 'public.bin'
    flags = ['--commands', str(commands), '--execute', '--backend', 'native', '--quirk-mode', 'off',
             '--trace-jsonl', str(rows), '--trace-meta', str(meta), '--readback-buffer', f'701={pixels}']
    result = subprocess.run([str(args.binary), *flags, '--backend-lane', 'VULKAN-DAWN-DIRECTIONAL'],
                            cwd=REPO, capture_output=True, text=True, timeout=20)
    assert result.returncode == 0, result.stderr
    metadata = json.loads(meta.read_text())
    trace = [json.loads(line) for line in rows.read_text().splitlines()]
    check_schemas('public', metadata, trace)
    assert metadata['backendLane'] == 'vulkan_dawn_release'
    assert metadata['adapterInfo']['adapterType'] == 'cpu'
    assert metadata['executionSuccessCount'] == 2 and metadata['module'] == 'dawn-c-runtime'
    receipts = metadata['shaderSourceReceipts']
    assert len(receipts) == 1 and receipts[0]['moduleId'] == 'doe-public-render'
    assert receipts[0]['byteLength'] > 300 and len(receipts[0]['sha256']) == 64
    assert replay_passes(args.work, 'public')
    data = pixels.read_bytes()
    colors = [(255, 0, 0, 255), (0, 255, 0, 255), (0, 0, 255, 255), (255, 255, 0, 255)]
    for y in range(32):
        for x in range(32):
            px, py = x + 0.5, y + 0.5
            inside = 8 < py < 24 and 16 - (py - 8) / 2 < px < 16 + (py - 8) / 2
            expected = colors[(2 if y >= 16 else 0) + (1 if x >= 16 else 0)] if inside else (0, 0, 0, 255)
            assert tuple(data[y * 256 + x * 4:y * 256 + x * 4 + 4]) == expected, (x, y)
    for binary, lane in [(args.native, 'vulkan_dawn_release'), (args.binary, 'metal_dawn_release'),
                         (args.binary, 'vulkan_doe_app')]:
        failed = subprocess.run([str(binary), *flags, '--backend-lane', lane], cwd=REPO,
                                capture_output=True, text=True, timeout=10)
        assert failed.returncode == 2 and '--backend-lane' in failed.stderr, failed.stderr
    print('PASS explicit Dawn delegate CLI route, canonical lane, truthful shader receipt, all pixels and provider refusals:',
          metadata['adapterInfo'])


if __name__ == '__main__':
    main()
