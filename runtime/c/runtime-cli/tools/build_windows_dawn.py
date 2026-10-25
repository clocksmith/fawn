#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Produce the real pinned Windows Dawn DLL/import library and C CLI, without GPU execution."""
from __future__ import annotations
import argparse
import ast
import json
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from build_windows import REPO, ROOT, digest, invoke

DAWN_REVISION = 'f99976772eb1e7ae4ccb49cbdeb1b7b6ad32a244'
DAWN_URL = 'https://dawn.googlesource.com/dawn'


def capture(command: list[str], directory: Path) -> str:
    return subprocess.run(command, cwd=directory, capture_output=True, text=True, check=True, timeout=30).stdout.strip()


def dependency_revisions(source: Path) -> dict[str, str]:
    """Verify every initialized gitlink against its immutable parent tree."""
    revisions: dict[str, str] = {'.': capture(['git', 'rev-parse', 'HEAD'], source)}
    pending = [source]
    while pending:
        parent = pending.pop()
        entries = capture(['git', 'ls-files', '--stage'], parent)
        for line in entries.splitlines():
            mode, revision, rest = line.split(' ', 2)
            if mode != '160000':
                continue
            path = parent / rest.split('\t', 1)[1]
            if not (path / '.git').exists():
                continue
            actual = capture(['git', 'rev-parse', 'HEAD'], path)
            if actual != revision:
                raise ValueError(f'dependency revision mismatch: {path.relative_to(source)}')
            revisions[path.relative_to(source).as_posix()] = actual
            pending.append(path)
    tree = ast.parse((source / 'tools/fetch_dawn_dependencies.py').read_text())
    required = [ast.literal_eval(node.value) for node in ast.walk(tree)
                if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and
                   target.id == 'required_submodules' for target in node.targets)]
    if len(required) != 1 or any(path not in revisions for path in required[0]):
        raise ValueError('not every official fetched dependency has verified immutable provenance')
    return revisions


def abi_probe(header: str) -> str:
    """Compare all pinned enum values, public record sizes/alignment/offsets."""
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', header, flags=re.S)
    statements: list[str] = []
    for name, body in re.findall(r'typedef\s+enum\s+(WGPU\w+)\s*\{(.*?)\}', text, re.S):
        for value in re.findall(r'\b(WGPU\w+)\s*=', body):
            statements.append(f'printf("{value}=%llu\\n", (unsigned long long){value});')
    for name, body in re.findall(r'typedef\s+struct\s+(WGPU\w+)\s*\{(.*?)\}', text, re.S):
        statements.append(f'printf("{name}=%zu,%zu\\n", sizeof({name}), _Alignof({name}));')
        for field in body.split(';'):
            if not field.strip():
                continue
            match = re.search(r'(\w+)\s*(?:\[[^]]+\])?\s*$', field)
            if not match:
                raise ValueError(f'unrecognized pinned record field: {name}')
            member = match[1]
            statements.append(f'printf("{name}.{member}=%zu\\n", offsetof({name}, {member}));')
    if len(statements) < 500:
        raise ValueError('unexpectedly incomplete pinned ABI probe')
    return '#include <stddef.h>\n#include <stdio.h>\n#include ABI_HEADER\nint main(void) {\n' + '\n'.join(statements) + '\nreturn 0;\n}\n'



def deterministic_git_checkout() -> None:
    """Scope byte-preserving checkout rules to this producer and its children."""
    count = int(os.environ.get('GIT_CONFIG_COUNT', '0'))
    for key, value in [('core.autocrlf', 'false'), ('core.eol', 'lf')]:
        os.environ[f'GIT_CONFIG_KEY_{count}'] = key
        os.environ[f'GIT_CONFIG_VALUE_{count}'] = value
        count += 1
    os.environ['GIT_CONFIG_COUNT'] = str(count)


def verify_header_bytes(header: Path, expected: str, out: Path) -> None:
    raw = header.read_bytes()
    blob = subprocess.run(['git', 'show', 'HEAD:webgpu.h'], cwd=header.parent,
                          capture_output=True, check=True, timeout=30).stdout
    def describe(data: bytes) -> dict:
        return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
                'lf': data.count(b'\n'), 'crlf': data.count(b'\r\n')}
    evidence = {'expectedSha256': expected, 'checkout': describe(raw), 'gitBlob': describe(blob),
                'revision': capture(['git', 'rev-parse', 'HEAD'], header.parent)}
    (out / 'header-integrity.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(json.dumps({'headerIntegrity': evidence}), flush=True)
    if evidence['checkout']['sha256'] != expected or evidence['gitBlob']['sha256'] != expected:
        raise ValueError('fetched upstream WebGPU header hash mismatch (see header-integrity.json)')


def artifact_query(path: Path) -> None:
    """Ask CMake for this target's actual outputs after upstream overrides."""
    path.write_text('file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/doe-dawn-artifacts.txt"\n'
                    '  CONTENT "$<TARGET_FILE:webgpu_dawn>\\n'
                    '$<TARGET_LINKER_FILE:webgpu_dawn>\\n")\n', encoding='utf-8')


def stage_artifacts(build: Path, products: Path) -> dict[str, dict[str, str]]:
    """Stage only the named target's DLL and import library, failing closed."""
    manifest = build / 'doe-dawn-artifacts.txt'
    paths = manifest.read_text(encoding='utf-8').splitlines()
    if len(paths) != 2:
        raise ValueError('invalid Dawn target artifact manifest')
    selected = []
    for value, name in zip(paths, ['webgpu_dawn.dll', 'webgpu_dawn.lib']):
        path = Path(value)
        if not path.is_absolute():
            raise ValueError('Dawn target artifact must be absolute')
        path = path.resolve()
        if not any(path.is_relative_to(root.resolve()) for root in [build, products]):
            raise ValueError('Dawn target artifact escapes build outputs')
        if path.name != name or not path.is_file():
            raise ValueError('actual Dawn target DLL/import library missing or misnamed')
        selected.append(path)
    products.mkdir(parents=True, exist_ok=True)
    evidence = {}
    for path in selected:
        destination = products / path.name
        if path != destination.resolve():
            shutil.copy2(path, destination)
        if digest(destination) != digest(path):
            raise ValueError('staged Dawn artifact hash mismatch')
        evidence[path.name] = {'target_path': path.relative_to(build.parent).as_posix(),
                               'sha256': digest(destination)}
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires the actual Windows SDK developer environment')
    out = args.out.resolve()
    if out == REPO or REPO in out.parents or out.exists() and any(out.iterdir()):
        parser.error('out must be fresh or empty and outside checkout')
    out.mkdir(parents=True, exist_ok=True)
    source, build, products = out / 'source', out / 'build', out / 'products'
    source.mkdir()
    deterministic_git_checkout()
    invoke(['git', 'init', str(source)], out)
    invoke(['git', 'remote', 'add', 'origin', DAWN_URL], source)
    invoke(['git', 'fetch', '--depth=1', 'origin', DAWN_REVISION], source, 300)
    invoke(['git', 'checkout', '--detach', 'FETCH_HEAD'], source)
    if capture(['git', 'rev-parse', 'HEAD'], source) != DAWN_REVISION:
        raise ValueError('Dawn source revision mismatch')
    policy = json.loads((REPO / 'config/webgpu-abi-source.json').read_text())['source']
    header = REPO / policy['path']
    if digest(header) != policy['sha256'] or '@' + policy['commit'] not in (source / 'DEPS').read_text():
        raise ValueError('Dawn DEPS does not match the exact pinned WebGPU ABI')
    query = out / 'target-artifacts.cmake'
    artifact_query(query)
    configure = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja',
                 '-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=' + str(query),
                 '-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=OFF', '-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED',
                 '-DDAWN_FETCH_DEPENDENCIES=ON', '-DDAWN_BUILD_TESTS=OFF', '-DTINT_BUILD_TESTS=OFF',
                 '-DDAWN_BUILD_SAMPLES=OFF', '-DDAWN_BUILD_BENCHMARKS=OFF', '-DTINT_BUILD_BENCHMARKS=OFF',
                 '-DTINT_BUILD_CMD_TOOLS=OFF', '-DDAWN_USE_GLFW=OFF', '-DDAWN_ENABLE_D3D12=ON',
                 '-DDAWN_ENABLE_D3D11=OFF', '-DDAWN_ENABLE_VULKAN=OFF', '-DDAWN_ENABLE_METAL=OFF',
                 '-DDAWN_ENABLE_OPENGLES=OFF', '-DDAWN_ENABLE_DESKTOP_GL=OFF', '-DDAWN_USE_BUILT_DXC=OFF',
                 '-DDAWN_USE_AGILITY_SDK=OFF', '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=' + str(products),
                 '-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=' + str(products)]
    invoke(configure, out, 600)
    dependencies = dependency_revisions(source)
    upstream_header = source / 'third_party/webgpu-headers/src/webgpu.h'
    verify_header_bytes(upstream_header, policy['sha256'], out)
    invoke(['cmake', '--build', str(build), '--target', 'webgpu_dawn', '--parallel', '2'], out, 1800)
    probe = out / 'abi-probe.c'
    probe.write_text(abi_probe(header.read_text()), encoding='utf-8')
    results = []
    for label, include, includes in [
            ('pinned', 'webgpu.h', [header.parent]),
            ('dawn', 'dawn/webgpu.h', [build / 'gen/include', source / 'include'])]:
        executable = out / ('abi-' + label + '.exe')
        concrete_probe = out / ('abi-' + label + '.c')
        concrete_probe.write_text(probe.read_text().replace('#include ABI_HEADER', '#include <' + include + '>'))
        invoke(['clang-cl', '/nologo', '/WX', '/clang:-std=c11',
                *('/I' + str(path) for path in includes), str(concrete_probe), '/Fe' + str(executable)], out)
        value = capture([str(executable)], out)
        (out / ('abi-' + label + '.txt')).write_text(value + '\n')
        results.append(value)
    if results[0] != results[1]:
        raise ValueError('generated Dawn ABI differs from pinned enum/record layout')
    artifacts = stage_artifacts(build, products)
    dll, imports = products / 'webgpu_dawn.dll', products / 'webgpu_dawn.lib'
    for relative in dependencies:
        owner = source if relative == '.' else source / relative
        destination = products / 'licenses' / ('dawn' if relative == '.' else relative)
        destination.mkdir(parents=True, exist_ok=True)
        for pattern in ['LICENSE*', 'COPYING*']:
            for license_file in owner.glob(pattern):
                if license_file.is_file():
                    shutil.copy2(license_file, destination / license_file.name)
    command = [sys.executable, str(ROOT / 'tools/build_windows.py'), '--provider', 'dawn',
               '--dawn-dll', str(dll), '--dawn-import', str(imports), '--out', str(out / 'cli')]
    for path in sorted(products.glob('*.dll')):
        if path != dll:
            command.extend(['--dawn-runtime', str(path)])
    invoke(command, out, 600)
    relocated = out / 'relocated'
    relocated.mkdir()
    for path in (out / 'cli').iterdir():
        if path.suffix.lower() in ['.dll', '.exe']:
            shutil.copy2(path, relocated / path.name)
    invoke([sys.executable, str(ROOT / 'tests/test_windows_cli.py'), '--provider', 'dawn',
            '--binary', str(relocated / 'dawn-c-runtime.exe'), '--work', str(out / 'relocation-controls')], out)
    (out / 'dawn-producer.json').write_text(json.dumps({
        'source': DAWN_URL, 'revision': DAWN_REVISION, 'dependency_revisions': dependencies,
        'deps_sha256': digest(source / 'DEPS'), 'header_sha256': digest(header),
        'generated_header_sha256': digest(build / 'gen/include/dawn/webgpu.h'),
        'target_artifacts': artifacts,
        'target_manifest_sha256': digest(build / 'doe-dawn-artifacts.txt'),
        'configuration': configure, 'cache_sha256': digest(build / 'CMakeCache.txt'),
        'toolchain': capture(['clang-cl', '--version'], out),
        'products_sha256': {path.name: digest(path) for path in products.iterdir() if path.is_file()},
        'cli_receipt_sha256': digest(out / 'cli/cli-scope.json'),
        'scope': 'Actual Windows Dawn build, ABI/loader/host/relocation controls; no adapter, GPU or performance qualification.'
    }, indent=2, sort_keys=True) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
