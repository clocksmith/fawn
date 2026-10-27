#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build Fawn.exe, the private C-default browser launcher, using actual Windows tools."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess

LANE = Path(__file__).resolve().parents[1]
SOURCE = LANE / 'launcher/fawn_windows.c'
MANIFEST = LANE / 'launcher/fawn_windows.manifest'


def digest(file: Path) -> str:
    return hashlib.sha256(file.read_bytes()).hexdigest()


def build(clang: Path, vs: Path, output: Path) -> dict:
    if os.name != 'nt':
        raise ValueError('requires actual Windows; no host or compiler fallback')
    if output.exists() and any(output.iterdir()):
        raise ValueError('launcher output must be fresh; retain previous evidence separately')
    output.mkdir(parents=True, exist_ok=True)
    helper = LANE / 'scripts/build-windows.py'
    env = runpy.run_path(str(helper))['developer_environment'](vs)
    sdk = Path(env['WindowsSdkDir'])
    version = env['WindowsSDKVersion'].rstrip('\\/')
    vc = Path(env['VCToolsInstallDir'])
    inputs = [SOURCE, MANIFEST, Path(__file__), helper, clang,
              sdk / f'Include/{version}/um/Windows.h',
              sdk / f'Lib/{version}/um/x64/kernel32.lib',
              sdk / f'Lib/{version}/um/x64/shell32.lib',
              sdk / f'Lib/{version}/um/x64/user32.lib',
              vc / 'include/vcruntime.h', vc / 'lib/x64/libcmt.lib']
    snapshots = output / 'source-inputs'
    snapshots.mkdir()
    for file in (SOURCE, MANIFEST, Path(__file__), helper):
        (snapshots / file.name).write_bytes(file.read_bytes())
    receipt = {'kind': 'windows-fawn-launcher-build', 'schemaVersion': 1,
               'scope': 'launcher binary only; no Chromium/provider/browser qualification',
               'inputs': {str(file): digest(file) for file in inputs},
               'compilerVersion': subprocess.check_output([str(clang), '--version'], text=True),
               'windowsSdkVersion': version, 'vcToolsVersion': env['VCToolsVersion'],
               'completed': False}
    command = [str(clang), '--target=x86_64-pc-windows-msvc', '/std:c11', '/W4', '/WX',
               '/O2', '/MT', str(SOURCE), '/Fe:' + str(output / 'Fawn.exe'),
               '/Fo:' + str(output / 'fawn_windows.obj'), '/link', '/SUBSYSTEM:WINDOWS',
               'shell32.lib', 'user32.lib', '/MANIFEST:EMBED', '/MANIFESTINPUT:' + str(MANIFEST)]
    receipt['command'] = command
    record = output / 'launcher-build.json'
    record.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    with (output / 'compile.stdout').open('wb') as stdout, (output / 'compile.stderr').open('wb') as stderr:
        result = subprocess.run(command, cwd=output, env=env, stdout=stdout, stderr=stderr)
    receipt['exitCode'] = result.returncode
    drift = [str(file) for file in inputs if digest(file) != receipt['inputs'][str(file)]]
    receipt['inputDrift'] = drift
    if result.returncode == 0 and not drift:
        receipt['completed'] = True
        receipt['launcherSha256'] = digest(output / 'Fawn.exe')
    record.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    if result.returncode != 0:
        raise RuntimeError(f'launcher build failed: {result.returncode}; see {output / "compile.stderr"}')
    if drift:
        raise RuntimeError(f'launcher inputs drifted during build: {drift}')
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang-cl', type=Path, required=True)
    parser.add_argument('--vs-install', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(build(args.clang_cl.resolve(), args.vs_install.resolve(), args.out.resolve()), indent=2))


if __name__ == '__main__':
    main()
