#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile public ABI11 consumer fixtures with actual DXC; no GPU execution.

First run test_hlsl_api with a fresh existing output directory as its sole
argument. This optional target check preserves each first failure and compares
DXC's actual resource/wave declarations with the public compiler metadata.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def audit_reflection(assembly: str, metadata: dict, entry_name: str) -> None:
    entry = next(record for record in metadata['entries'] if record['name'] == entry_name)
    namespaces = {1: 'cb', 2: 't', 3: 'u', 4: 's'}
    expected = {(namespaces[record['kind']], record['register'], record['space'])
                for record in metadata['bindings']}
    if entry['dispatchBytes']:
        expected.add(('cb', entry['dispatchRegister'], entry['dispatchSpace']))
    if entry['flags'] & 64:  # STORAGE_SIZES: runtime size table at b2, space7.
        expected.add(('cb', 2, 7))
    table = assembly.split('; Resource Bindings:', 1)[1].split('target datalayout', 1)[0]
    rows = re.findall(r'^;\s+\w+\s+(?:cbuffer|sampler|texture|UAV)\s+.+?\s+'
                      r'(cb|[stu])(\d+)(?:,space(\d+))?\s+\d+\s*$', table, re.M)
    actual = {(kind, int(register), int(space or 0)) for kind, register, space in rows}
    if actual != expected:
        raise ValueError(f'{entry_name}: DXC bindings {sorted(actual)} != ABI11 {sorted(expected)}')
    waves = 'Wave level operations' in assembly
    if waves != bool(entry['flags'] & 1):
        raise ValueError(f'{entry_name}: DXC wave requirement differs from ABI11')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dxc', type=Path, required=True, help='Actual DXC executable')
    parser.add_argument('--fixtures', type=Path, required=True, help='Public C consumer output directory')
    parser.add_argument('--output', type=Path, required=True, help='Fresh result directory; never replaced')
    args = parser.parse_args()
    dxc, fixtures, output = args.dxc.resolve(), args.fixtures.resolve(), args.output.absolute()
    if output.exists() or output.is_symlink():
        raise FileExistsError(f'Refusing existing DXC result directory: {output}')
    output.mkdir(parents=True)
    inputs = {path.name: sha256(path) for path in sorted(fixtures.iterdir())
              if path.suffix in ('.hlsl', '.json')}
    runs = []
    controls = [('first', 'first', None, 'first'), ('selected', 'selected', None, 'selected'),
                ('bundle', 'first', 0, 'first'), ('bundle', 'selected', 1, 'selected'),
                ('bundle', 'last', 2, None)]
    for stem, entry, selector, reflection_stem in controls:
        name = f'{stem}-{entry}'
        command = [str(dxc), '-T', 'cs_6_0', '-E', entry, str(fixtures / (stem + '.hlsl')),
                   '-Fo', str(output / (name + '.dxil')), '-Fc', str(output / (name + '.asm'))]
        if selector is not None:
            command += ['-D', f'DOE_HLSL_ENTRY={selector}']
        with (output / (name + '.log')).open('wb') as log:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False)
        row = {'command': command, 'exit_code': result.returncode}
        runs.append(row)
        if result.returncode:
            break
        metadata = json.loads((fixtures / ((reflection_stem or 'bundle') + '.json')).read_text(encoding='utf-8'))
        if reflection_stem is None:
            metadata['bindings'] = []  # The authored last entry has no resources.
        try:
            audit_reflection((output / (name + '.asm')).read_text(encoding='utf-8'), metadata, entry)
            row['metadata_match'] = True
        except (ValueError, IndexError, KeyError, StopIteration) as error:
            row['metadata_match'] = False
            row['error'] = str(error)
            break
    after = {name: sha256(fixtures / name) for name in inputs}
    if after != inputs:
        raise RuntimeError('Public HLSL fixtures changed during DXC validation')
    passed = len(runs) == len(controls) and all(row.get('metadata_match') for row in runs)
    receipt = {'passed': passed, 'inputs': inputs, 'runner_sha256': sha256(Path(__file__)),
               'dxc_sha256': sha256(dxc), 'dxc_version': subprocess.check_output(
                   [str(dxc), '--version'], text=True).strip(), 'runs': runs,
               'artifacts': {path.name: sha256(path) for path in sorted(output.iterdir())},
               'scope': 'Actual HLSL/DXIL compilation and reflected binding/wave comparison; no GPU execution'}
    (output / 'RESULT.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    print(f'{"PASS" if passed else "FAIL"} public ABI11 DXC: {len(runs)} controls; {output / "RESULT.json"}')
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
