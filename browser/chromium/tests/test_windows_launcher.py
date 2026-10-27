#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run actual Windows argv, CDP CRT pipes, private-provider policy and Job controls.

The compiled chrome.exe fixture is explicitly a process probe. Passing does not
qualify Chromium, GPU execution, a provider library or a browser distribution.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess

LANE = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-cl', type=Path, required=True)
    parser.add_argument('--vs-install', type=Path, required=True)
    parser.add_argument('--node', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires actual Windows')
    output = args.out.resolve()
    producer = runpy.run_path(str(LANE / 'scripts/build-fawn-launcher-windows.py'))
    producer['build'](args.clang_cl.resolve(), args.vs_install.resolve(), output)
    environment = runpy.run_path(str(LANE / 'scripts/build-windows.py'))['developer_environment'](args.vs_install.resolve())
    fixture = LANE / 'tests/windows_process_probe.c'
    controls = LANE / 'tests/windows-launcher-controls.cjs'
    command = [str(args.clang_cl.resolve()), '--target=x86_64-pc-windows-msvc',
               '/std:c11', '/W4', '/WX', '/O2', '/MT', str(fixture),
               '/Fe:' + str(output / 'chrome.exe'), '/Fo:' + str(output / 'probe.obj'),
               '/link', '/SUBSYSTEM:CONSOLE']
    with (output / 'probe.stdout').open('wb') as stdout, (output / 'probe.stderr').open('wb') as stderr:
        result = subprocess.run(command, cwd=output, env=environment, stdout=stdout, stderr=stderr)
    identity = {'scope': __doc__, 'fixtureCommand': command, 'fixtureExitCode': result.returncode,
                'inputs': {str(file): hashlib.sha256(file.read_bytes()).hexdigest()
                           for file in (fixture, controls, Path(__file__), args.node.resolve())}}
    receipt = output / 'test-inputs.json'
    receipt.write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError(f'process fixture compilation failed: {result.returncode}')
    command = [str(args.node.resolve()), str(controls), str(output)]
    result = subprocess.run(command, cwd=output, env=environment, timeout=180)
    if result.returncode != 0:
        raise RuntimeError(f'actual Windows launcher controls failed: {result.returncode}')
    for name, digest in identity['inputs'].items():
        if hashlib.sha256(Path(name).read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'launcher test input drifted: {name}')


if __name__ == '__main__':
    main()
