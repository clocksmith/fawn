#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the standalone x64 Windows compiler DLL with the real SDK and clang-cl.

Source lists and exports are the existing owners' inventories. No POSIX CLI,
runtime backend, device execution or production promotion is implied.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from export_symbols import symbols


ROOT = Path(__file__).resolve().parents[1]


def source_paths() -> list[Path]:
    paths = ROOT.joinpath('src/sources.txt').read_text().split()
    for target, variable in [('metal', 'METAL_SOURCE_NAMES'), ('hlsl', 'HLSL_SOURCE_NAMES')]:
        text = ROOT.joinpath('targets', target, 'sources.mk').read_text()
        lines = [line for line in text.splitlines() if line and not line.startswith('#')]
        if len(lines) != 1 or not lines[0].startswith(variable + ' = '):
            raise ValueError(f'unsupported {target} source inventory')
        paths.extend('targets/' + target + '/' + name for name in lines[0].split(' = ', 1)[1].split())
    if len(paths) != len(set(paths)):
        raise ValueError('duplicate compiler source')
    for path in paths:
        if not re.fullmatch(r'(src|targets/(metal|hlsl))/[a-z0-9_]+\.c', path):
            raise ValueError(f'invalid compiler source path: {path}')
        if not ROOT.joinpath(path).is_file():
            raise FileNotFoundError(path)
    return [ROOT / path for path in paths]


def invoke(command: list[str], directory: Path) -> None:
    print(subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=directory, check=True, timeout=180)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--clang-cl', default='clang-cl')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires an actual x64 Windows SDK developer environment')
    compiler = shutil.which(args.clang_cl)
    if compiler is None:
        parser.error('clang-cl is required; no compiler fallback is permitted')
    out = args.out.resolve()
    if out == ROOT or ROOT in out.parents:
        parser.error('out must be outside the source owner')
    if out.exists() and any(out.iterdir()):
        parser.error('out must be fresh or empty; retain earlier build evidence separately')
    out.mkdir(parents=True, exist_ok=True)
    names = symbols(ROOT.joinpath('src/exports.map').read_text())
    export_file = out / 'compiler.def'
    export_file.write_text('EXPORTS\n' + '\n'.join(names) + '\n')
    strict = [compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX',
              '/D_CRT_SECURE_NO_WARNINGS', '/clang:-std=c11', '/clang:-fno-fast-math',
              '/clang:-ffp-contract=off', '/clang:-frounding-math', '/clang:-Wall',
              '/clang:-Wextra', '/clang:-Wpedantic', '/clang:-Wconversion',
              '/clang:-Wshadow', '/clang:-Wstrict-prototypes', '/clang:-Wmissing-prototypes',
              '/clang:-Wswitch-enum']
    sources = source_paths()
    objects = []
    for source in sources:
        obj = out / (source.relative_to(ROOT).as_posix().replace('/', '_') + '.obj')
        invoke(strict + ['/c', str(source), '/Fo' + str(obj)], out)
        objects.append(str(obj))
    library = out / 'doe_compiler.dll'
    import_library = out / 'doe_compiler.lib'
    invoke([compiler, '/nologo', '/LD', *objects, '/Fe' + str(library), '/link',
            '/def:' + str(export_file), '/implib:' + str(import_library)], out)
    exports = subprocess.run(['dumpbin', '/nologo', '/exports', str(library)],
                             cwd=out, check=True, timeout=30, capture_output=True, text=True)
    (out / 'exports.txt').write_text(exports.stdout + exports.stderr)
    actual = re.findall(r'^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\w+)\s*$',
                        exports.stdout, re.MULTILINE)
    if sorted(actual) != sorted(names):
        raise ValueError(f'actual Windows DLL exports differ from canonical map: {actual}')
    # Genuine ordinary consumers exercise the DLL, not duplicated production code.
    consumers = [ROOT / 'tests/native_consumer.c', ROOT / 'tests/test_phony_api.c',
                 ROOT / 'tests/test_windows_environment.c', ROOT / 'tests/test_overrides.c',
                 ROOT / 'tests/test_specialization.c', ROOT / 'tests/test_select.c',
                 ROOT / 'tests/test_depth_sampling.c']
    for source in consumers:
        name = source.stem
        executable = out / (name + '.exe')
        invoke(strict + [str(source),
                         '/Fo' + str(out / (name + '.obj')), '/Fe' + str(executable),
                         '/link', str(import_library), '/stack:8388608'], out)
        arguments = [str(executable)]
        if name == 'native_consumer':
            arguments.append(str(ROOT.parents[2] / 'bench/inference-pipeline/kernels/residual.wgsl'))
        if name == 'test_select':
            arguments.extend(str(ROOT / 'tests' / source_name) for source_name in
                             ['select_retained.wgsl', 'select_cases.wgsl'])
        if name == 'test_depth_sampling':
            artifacts = tempfile.mkdtemp(prefix='depth-sampling-', dir=out)
            arguments.extend([artifacts, str(ROOT / 'tests/depth_sampling.wgsl')])
        invoke(arguments, out)
    cpp_consumers = [ROOT / 'tests/cpp_consumer.cpp', ROOT / 'tests/cpp_consumer_specialization.cpp']
    for source in cpp_consumers:
        executable = out / (source.stem + '.exe')
        invoke([compiler, '/nologo', '/MD', '/O2', '/WX', '/std:c++17',
                '/clang:-fno-exceptions', '/clang:-fno-rtti', str(source),
                '/Fo' + str(out / (source.stem + '.obj')),
                '/Fe' + str(executable), '/link', str(import_library), '/stack:8388608'], out)
        invoke([str(executable)], out)
    (out / 'scope.json').write_text(json.dumps({
        'compiler': compiler, 'sources': [str(path.relative_to(ROOT)) for path in sources],
        'exports': names, 'library': str(library),
        'consumers': [str(path.relative_to(ROOT)) for path in consumers + cpp_consumers],
        'consumer_inputs': ['tests/select_retained.wgsl', 'tests/select_cases.wgsl',
                            'tests/depth_sampling.wgsl'],
        'scope': 'Actual Windows x64 DLL and ordinary C/C++ CPU consumers only; no GPU, runtime or CLI qualification.'
    }, indent=2) + '\n')


if __name__ == '__main__':
    main()
