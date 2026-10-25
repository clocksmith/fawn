#!/usr/bin/env python3
"""Refuse drift of the retained timestamp policy and embedded WGSL body."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--repo', type=Path, required=True)
args = parser.parse_args()
owner = Path(__file__).resolve().parent.parent
provenance = json.loads((owner / 'TIMESTAMP_PROVENANCE.json').read_text())
for name, expected in provenance['sha256'].items():
    actual = hashlib.sha256((args.repo / name).read_bytes()).hexdigest()
    if actual != expected:
        raise SystemExit(f'retained timestamp source drift: {name}')
embedded = ''.join(json.loads(line.strip().removesuffix(';')) for line in
                   (args.repo / 'runtime/c/common/include/doe_timestamp_kernel.h').read_text().splitlines()
                   if line.lstrip().startswith('"'))
original = (args.repo / 'runtime/zig/src/backend/vulkan/vk_timestamp_normalize.wgsl').read_text()
if embedded != original:
    raise SystemExit('embedded timestamp kernel differs from pinned retained WGSL')
print('PASS retained timestamp policy hashes and verbatim embedded WGSL body')
