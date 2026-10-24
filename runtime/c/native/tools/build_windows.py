#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the genuine native WebGPU DLL with Windows SDK clang-cl.

Canonical owner/source and export inventories are shared with the native build.
Only the typed loader host control executes; no adapter/device or GPU is used.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
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


def owner_paths() -> list[Path]:
    lines = [line for line in ROOT.joinpath('sources.mk').read_text(encoding="utf-8").splitlines()
             if line and not line.startswith('#')]
    prefixes = ['NATIVE_OWNER_DIRS = ', 'NATIVE_WINDOWS_OWNER_DIRS = ']
    if len(lines) != len(prefixes) or any(not line.startswith(prefix)
            for line, prefix in zip(lines, prefixes)):
        raise ValueError('unsupported canonical native owner inventory')
    owners = [owner for line, prefix in zip(lines, prefixes) for owner in line[len(prefix):].split()]
    if not owners or len(set(owners)) != len(owners):
        raise ValueError('native owners must be nonempty and unique')
    result = []
    for owner in owners:
        if not re.fullmatch(r'\.\./[a-z0-9_-]+(?:/[a-z0-9_-]+)?', owner):
            raise ValueError(f'invalid native owner: {owner}')
        path = ROOT.joinpath(owner).resolve()
        if not path.is_dir() or not path.joinpath('include').is_dir():
            raise ValueError(f'missing native owner/include: {owner}')
        result.append(path)
    return result


def source_paths(owners: list[Path]) -> list[Path]:
    sources = sorted(ROOT.joinpath('src').glob('*.c'))
    for owner in owners:
        sources.extend(sorted(owner.joinpath('src').glob('*.c')))
    if len(set(sources)) != len(sources):
        raise ValueError('duplicate native source')
    return sources


def export_names() -> list[str]:
    names = []
    for inventory in ['EXPORTED_SYMBOLS.txt', 'EXTENSION_SYMBOLS.txt']:
        names.extend(ROOT.joinpath(inventory).read_text(encoding="utf-8").splitlines())
    if not names or len(set(names)) != len(names) or any(
            re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name) is None for name in names):
        raise ValueError('native export inventories must contain unique explicit C names')
    return names


def invoke(command: list[str], directory: Path) -> None:
    print(subprocess.list2cmdline(command), flush=True)
    subprocess.run(command, cwd=directory, check=True, timeout=240)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compiler_inputs(directory: Path) -> tuple[Path, Path]:
    library = directory / 'doe_compiler.dll'
    imports = directory / 'doe_compiler.lib'
    scope = json.loads(directory.joinpath('scope.json').read_text(encoding="utf-8"))
    # Existing compiler builder audits actual exports before writing scope.json.
    module_path = C_ROOT / 'compiler/tools/export_symbols.py'
    spec = importlib.util.spec_from_file_location('compiler_exports', module_path)
    if spec is None or spec.loader is None:
        raise ImportError(f'cannot load canonical compiler export owner: {module_path}')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    canonical = module.symbols(C_ROOT.joinpath('compiler/src/exports.map').read_text(encoding="utf-8"))
    if sorted(scope['exports']) != sorted(canonical) or not library.is_file() or not imports.is_file():
        raise ValueError('compiler producer output is incomplete or has a different public interface')
    return library, imports


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True, help='fresh output outside source checkout')
    parser.add_argument('--compiler-dir', type=Path, help='audited existing compiler DLL builder output')
    parser.add_argument('--dxc-root', type=Path, required=True,
                        help='official Windows DXC package containing inc and bin/x64')
    parser.add_argument('--clang-cl', default='clang-cl', help='actual clang-cl executable')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires an actual x64 Windows SDK developer environment')
    compiler = shutil.which(args.clang_cl)
    if compiler is None:
        parser.error('clang-cl is required; no compiler fallback is permitted')
    out = args.out.resolve()
    if out == REPO or REPO in out.parents:
        parser.error('out must be outside the source checkout')
    if out.exists() and any(out.iterdir()):
        parser.error('out must be fresh or empty; retain previous evidence separately')
    out.mkdir(parents=True, exist_ok=True)
    version = subprocess.run([compiler, '--version'], check=True, timeout=30,
                             capture_output=True, text=True)
    out.joinpath('native-toolchain.txt').write_text(version.stdout + version.stderr, encoding='utf-8')
    producer = args.compiler_dir.resolve() if args.compiler_dir else out / 'compiler'
    if args.compiler_dir is None:
        invoke([sys.executable, str(C_ROOT / 'compiler/tools/build_windows.py'),
                '--out', str(producer), '--clang-cl', compiler], out)
    compiler_library, compiler_imports = compiler_inputs(producer)
    shutil.copy2(compiler_library, out / compiler_library.name)
    dxc = args.dxc_root.resolve()
    dxc_headers = sorted(dxc.joinpath('inc').rglob('*.h'))
    dxc_runtime = [dxc / 'bin/x64/dxcompiler.dll', dxc / 'bin/x64/dxil.dll']
    licenses = sorted(path for path in dxc.iterdir() if path.is_file() and
                      path.name.casefold().startswith('license'))
    if not dxc.joinpath('inc/dxcapi.h').is_file() or any(not dxc.joinpath(name).is_file()
            for name in ['LICENSE-LLVM.txt', 'LICENSE-MS.txt']) or any(
            not path.is_file() for path in dxc_runtime):
        parser.error('DXC package must contain official headers, both x64 runtime DLLs and licenses')
    license_output = out / 'dxc-licenses'
    license_output.mkdir()
    for path in dxc_runtime:
        shutil.copy2(path, out / path.name)
    for path in licenses:
        shutil.copy2(path, license_output / path.name)
    owners = owner_paths()
    sources = source_paths(owners)
    names = export_names()
    export_file = out / 'native.def'
    export_file.write_text('EXPORTS\n' + '\n'.join(names) + '\n', encoding='utf-8')
    schema = REPO / 'config/compute-program.schema.json'
    contract = json.loads(schema.read_text(encoding="utf-8"))['$defs']['nativeContractVersion']['const']
    if not isinstance(contract, int) or isinstance(contract, bool) or not 0 < contract <= 0xffffffff:
        raise ValueError('native program contract must be a positive uint32')
    includes = [ROOT / 'include', REPO / 'runtime/zig/vendor/webgpu-headers', C_ROOT / 'compiler/include']
    includes.extend(owner / 'include' for owner in owners)
    strict = [compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX',
              '/D_CRT_SECURE_NO_WARNINGS', '/DWGPU_SHARED_LIBRARY', '/DWGPU_IMPLEMENTATION',
              f'/DDOE_COMPUTE_PROGRAM_CONTRACT={contract}u', '/clang:-std=c11',
              '/clang:-Wall', '/clang:-Wextra', '/clang:-Wpedantic']
    strict.extend('/I' + str(path) for path in includes)
    objects = []
    for source in sources:
        obj = out / (source.relative_to(C_ROOT).as_posix().replace('/', '_') + '.obj')
        invoke(strict + ['/c', str(source), '/Fo' + str(obj)], out)
        objects.append(obj)
    shader_adapter = C_ROOT / 'backends/d3d12/src/windows_shader.cpp'
    shader_object = out / 'd3d12_windows_shader.obj'
    invoke([compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX',
            '/fp:strict', '/std:c++17', '/clang:-fno-exceptions', '/clang:-fno-rtti',
            '/clang:-Wall', '/clang:-Wextra', '/clang:-Wpedantic',
            '/I' + str(dxc / 'inc'), '/I' + str(C_ROOT / 'backends/d3d12/include'),
            '/c', str(shader_adapter), '/Fo' + str(shader_object)], out)
    objects.append(shader_object)
    library = out / 'webgpu_doe_c.dll'
    imports = out / 'webgpu_doe_c.lib'
    invoke([compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/LD',
            *map(str, objects), '/Fe' + str(library), '/link', str(compiler_imports),
            'kernel32.lib', 'd3d12.lib', 'dxgi.lib', 'dxguid.lib',
            '/def:' + str(export_file), '/implib:' + str(imports)], out)
    exports = subprocess.run(['dumpbin', '/nologo', '/exports', str(library)], cwd=out,
                             check=True, timeout=30, capture_output=True, text=True)
    out.joinpath('native-exports.txt').write_text(exports.stdout + exports.stderr, encoding='utf-8')
    actual = re.findall(r'^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+(\w+)\s*$',
                        exports.stdout, re.MULTILINE)
    if sorted(actual) != sorted(names):
        raise ValueError(f'actual native DLL exports differ from canonical interface: {actual}')
    # Link the actual private loader object, not a replacement Win32 API shim.
    loader_test = C_ROOT / 'backends/vulkan/tests/loader_win32.c'
    loader_objects = [obj for source, obj in zip(sources, objects[:len(sources)])
                      if source.parent == C_ROOT / 'backends/vulkan/src']
    if not loader_objects:
        raise ValueError('canonical Vulkan owner is required for the loader control')
    executable = out / 'loader_win32.exe'
    invoke(strict + [str(loader_test), '/Fo' + str(out / 'loader_win32.obj'),
                     '/Fe' + str(executable), '/link', *map(str, loader_objects), 'kernel32.lib'], out)
    invoke([str(executable)], out)
    headers = sorted(set(ROOT.joinpath('src').glob('*.h')) |
                     set(ROOT.joinpath('include').glob('*.h')) |
                     set(C_ROOT.joinpath('compiler/include').glob('*.h')) |
                     set(C_ROOT.joinpath('common/include').glob('*.h')))
    for owner in owners:
        headers.extend(sorted(owner.joinpath('include').glob('*.h')))
        headers.extend(sorted(owner.joinpath('src').glob('*.h')))
    inputs = [*sources, *headers, ROOT / 'sources.mk', ROOT / 'EXPORTED_SYMBOLS.txt',
              ROOT / 'EXTENSION_SYMBOLS.txt', Path(__file__).resolve(), schema, loader_test,
              shader_adapter, REPO / 'runtime/zig/vendor/webgpu-headers/webgpu.h']
    out.joinpath('native-scope.json').write_text(json.dumps({
        'compiler': compiler, 'windows_sdk_version': os.environ.get('WindowsSDKVersion'),
        'vc_tools_version': os.environ.get('VCToolsVersion'),
        'compiler_version_output': version.stdout + version.stderr,
        'owners': [path.relative_to(C_ROOT).as_posix() for path in owners],
        'sources': [path.relative_to(REPO).as_posix() for path in sources], 'exports': names,
        'input_sha256': {path.relative_to(REPO).as_posix(): digest(path) for path in inputs},
        'compiler_dll_sha256': digest(compiler_library), 'compiler_import_library_sha256': digest(compiler_imports),
        'dxc_package_input_sha256': {path.relative_to(dxc).as_posix(): digest(path)
                                    for path in [*dxc_headers, *dxc_runtime, *licenses]},
        'packaged_runtime_sha256': {path.name: digest(out / path.name)
                                   for path in [compiler_library, *dxc_runtime]},
        'shader_adapter_object_sha256': digest(shader_object),
        'native_dll_sha256': digest(library),
        'scope': 'Actual SDK native DLL build/export audit and Win32 loader CPU host control only; no GPU, native device execution or production qualification.'
    }, indent=2, sort_keys=True) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
