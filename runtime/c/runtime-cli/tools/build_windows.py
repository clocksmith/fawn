#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the C CLI and host modules with actual x64 Windows SDK clang-cl.

Runs host controls only. Native DLLs come from the audited native owner builder;
no shader, adapter, device, window or GPU execution is part of this command.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
C_ROOT = ROOT.parent
REPO = C_ROOT.parents[1]
MODULES = C_ROOT / 'modules'


def inventory(owner: Path, name: str) -> list[Path]:
    text = owner.joinpath('sources.mk').read_text(encoding='utf-8').replace('\\\n', ' ')
    rows = [line for line in text.splitlines() if line.startswith(name + ' = ')]
    if len(rows) != 1:
        raise ValueError(f'missing unique canonical source inventory: {name}')
    paths = rows[0].split(' = ', 1)[1].split()
    if not paths or len(paths) != len(set(paths)) or any(
            re.fullmatch(r'(?:\.\./runtime-cli/)?src/[a-z0-9_]+\.c', path) is None for path in paths):
        raise ValueError(f'invalid explicit canonical source paths: {name}')
    return [owner.joinpath(path).resolve() for path in paths]


def invoke(command: list[str], directory: Path) -> None:
    print(subprocess.list2cmdline(command), flush=True)
    process = subprocess.Popen(command, cwd=directory)
    try:
        result = process.wait(timeout=180)
    except subprocess.TimeoutExpired:
        subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], check=False, timeout=15)
        process.wait(timeout=15)
        raise
    if result:
        raise subprocess.CalledProcessError(result, command)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='fresh output outside checkout')
    parser.add_argument('--native-dir', type=Path, required=True, help='actual native owner Windows builder output')
    parser.add_argument('--clang-cl', default='clang-cl')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires an actual x64 Windows SDK developer environment')
    compiler = shutil.which(args.clang_cl)
    if compiler is None:
        parser.error('actual clang-cl is required; no cross-platform fallback')
    out = args.out.resolve()
    if out == REPO or REPO in out.parents or out.exists() and any(out.iterdir()):
        parser.error('out must be fresh or empty and outside checkout')
    native = args.native_dir.resolve()
    receipt = json.loads(native.joinpath('native-scope.json').read_text(encoding='utf-8'))
    runtime_names = ['webgpu_doe_c.dll', 'doe_compiler.dll', 'dxcompiler.dll', 'dxil.dll']
    expected = {'webgpu_doe_c.dll': receipt['native_dll_sha256'], **receipt['packaged_runtime_sha256']}
    for name in runtime_names:
        if digest(native / name) != expected[name]:
            raise ValueError(f'producer runtime digest mismatch: {name}')
    imports = native / 'webgpu_doe_c.lib'
    if not imports.is_file():
        parser.error('native owner import library is required')
    out.mkdir(parents=True, exist_ok=True)
    for name in runtime_names:
        shutil.copy2(native / name, out / name)
    version = subprocess.run([compiler, '--version'], capture_output=True, text=True, check=True, timeout=30)
    out.joinpath('toolchain.txt').write_text(version.stdout + version.stderr, encoding='utf-8')
    toggles = out / 'quirk_toggles.c'
    invoke([sys.executable, str(ROOT / 'tools/gen_toggle_registry.py'),
            str(REPO / 'config/quirk-toggle-registry.json'), str(toggles)], out)
    flags = [compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX', '/fp:strict',
             '/D_CRT_SECURE_NO_WARNINGS', '/D_WIN32_WINNT=0x0A00', '/DWGPU_SHARED_LIBRARY',
             '/clang:-std=c11', '/clang:-Wall', '/clang:-Wextra', '/clang:-Wpedantic',
             '/I' + str(ROOT / 'src'), '/I' + str(MODULES / 'src'), '/I' + str(C_ROOT / 'native/include'),
             '/I' + str(REPO / 'runtime/zig/vendor/webgpu-headers')]
    core = inventory(ROOT, 'CLI_CORE') + [toggles]
    runtime = inventory(ROOT, 'CLI_RUNTIME') + [ROOT / 'src/surface_windows.c']
    modules = inventory(MODULES, 'MODULE_SOURCES') + [toggles]
    startup = ROOT / 'src/windows_start.c'
    source_objects: dict[Path, Path] = {}

    def compile_source(source: Path) -> Path:
        if source in source_objects:
            return source_objects[source]
        name = source.relative_to(C_ROOT).as_posix().replace('/', '_') if source != toggles else source.name
        obj = out / (name + '.obj')
        rename = ['/Dmain=doe_host_main'] if source in [ROOT / 'src/main.c', MODULES / 'src/main.c'] else []
        invoke(flags + rename + ['/c', str(source), '/Fo' + str(obj)], out)
        source_objects[source] = obj
        return obj

    def build(name: str, sources: list[Path], libraries: list[str]) -> Path:
        objects = [compile_source(source) for source in sources]
        executable = out / (name + '.exe')
        invoke([compiler, '--target=x86_64-pc-windows-msvc', '/nologo', *map(str, objects),
                '/Fe' + str(executable), '/link', '/stack:8388608', *libraries], out)
        return executable

    cli = build('doe-c-runtime', [ROOT / 'src/main.c', startup, *core, *runtime],
                [str(imports), 'kernel32.lib', 'user32.lib'])
    module = build('doe-c-module-runner', [*modules, startup], ['kernel32.lib'])
    for name, arguments in [('host_io', []), ('json', []), ('stream', [str(REPO / 'examples')]),
                            ('sha256', []), ('quirks', [str(REPO / 'examples')]), ('trace', []),
                            ('numeric', [str(REPO), str(out)])]:
        executable = build('test_' + name, [ROOT / f'tests/test_{name}.c', *core], ['kernel32.lib'])
        invoke([str(executable), *arguments], out)
    invoke([sys.executable, str(MODULES / 'tests/test_modules.py'), '--binary', str(module),
            '--work', str(out / 'module-controls')], out)
    invoke([sys.executable, str(ROOT / 'tests/test_windows_cli.py'), '--binary', str(cli),
            '--work', str(out / 'cli-controls')], out)
    inputs = [*source_objects, ROOT / 'sources.mk', MODULES / 'sources.mk', Path(__file__).resolve(),
              ROOT / 'tests/test_windows_cli.py', ROOT / 'tests/normalized_retained.json',
              MODULES / 'tests/test_modules.py', MODULES / 'tests/cases.py', MODULES / 'tests/retained.json',
              ROOT / 'tools/gen_toggle_registry.py', REPO / 'config/quirk-toggle-registry.json',
              REPO / 'runtime/zig/vendor/webgpu-headers/webgpu.h']
    for owner in [ROOT / 'src', MODULES / 'src', C_ROOT / 'platform/include', C_ROOT / 'native/include']:
        inputs.extend(sorted(owner.glob('*.h')))
    out.joinpath('cli-scope.json').write_text(json.dumps({
        'compiler': compiler, 'windows_sdk_version': os.environ.get('WindowsSDKVersion'),
        'vc_tools_version': os.environ.get('VCToolsVersion'),
        'compiler_version_output': version.stdout + version.stderr,
        'input_sha256': {path.relative_to(REPO).as_posix(): digest(path) for path in inputs if path != toggles},
        'generated_toggle_sha256': digest(toggles), 'native_import_sha256': digest(imports),
        'native_receipt_sha256': digest(native / 'native-scope.json'),
        'runtime_sha256': {name: digest(out / name) for name in runtime_names},
        'executable_sha256': {path.name: digest(path) for path in [cli, module]},
        'scope': 'Actual Windows SDK CLI/module build and host controls. No adapter, device, surface, GPU, performance or production qualification.'
    }, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
