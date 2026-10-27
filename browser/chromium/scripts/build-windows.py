#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build pinned Chromium plus the current C provider with native Windows tools.

This creates a diagnostic browser build, not a release-qualified distribution.
Use an isolated Chromium checkout; the selected pins and patches remain owned by
apply-runtime-patches.sh. No provider or compiler fallback is permitted.
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

LANE = Path(__file__).resolve().parents[1]
REPO = LANE.parents[1]
RUNTIME_FILES = ('webgpu_doe_c.dll', 'doe_compiler.dll', 'dxcompiler.dll', 'dxil.dll')
RELEASE_ARGS = '''is_debug = false
is_official_build = false
dcheck_always_on = false
chrome_pgo_phase = 0
symbol_level = 0
blink_symbol_level = 0
v8_symbol_level = 0
is_chrome_for_testing = false
is_chrome_for_testing_branded = false
is_chrome_branded = false
use_clang_modules = false
dawn_enable_webgpu_on_webgpu = true
target_cpu = "x64"
'''


def digest(file: Path) -> str:
    return hashlib.sha256(file.read_bytes()).hexdigest()


def source_pins(lane: Path = LANE) -> tuple[str, str]:
    script = (lane / 'scripts/apply-runtime-patches.sh').read_text(encoding='utf-8')
    return tuple(re.search(rf'^{name}="([a-f0-9]{{40}})"$', script, re.MULTILINE).group(1)
                 for name in ('CHROMIUM_REVISION', 'DAWN_REVISION'))


def patch_plan(src: Path, lane: Path = LANE, pins: tuple[str, str] | None = None) -> list[tuple[Path, Path]]:
    """Check both trees before applying either patch, preserving failed inputs."""
    pending = []
    for root, expected, patch in zip((src, src / 'third_party/dawn'), pins or source_pins(lane),
            (lane / 'patches/chromium-doe-runtime.patch', lane / 'patches/dawn-doe-runtime.patch')):
        actual = subprocess.check_output(['git', '-c', 'core.autocrlf=false', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != expected:
            raise ValueError(f'source revision mismatch: {root}: expected {expected}, received {actual}')
        command = ['git', '-c', 'core.autocrlf=false', '-C', str(root), 'apply']
        if subprocess.run(command + ['--reverse', '--check', str(patch)], capture_output=True).returncode == 0:
            continue
        subprocess.run(command + ['--check', str(patch)], check=True)
        pending.append((root, patch))
    return pending


def source_diffs(src: Path, output: Path, phase: str) -> list[dict[str, str]]:
    records = []
    for name, root in (('chromium', src), ('dawn', src / 'third_party/dawn')):
        data = subprocess.check_output(['git', '-c', 'core.autocrlf=false', '-C', str(root),
                                        'diff', '--binary', 'HEAD'])
        file = output / f'{phase}-{name}.diff'
        file.write_bytes(data)
        records.append({'repository': name, 'path': str(file),
                        'sha256': hashlib.sha256(data).hexdigest()})
    return records


def tool_inputs(src: Path, depot: Path, env: dict[str, str]) -> list[dict[str, str]]:
    sdk = Path(env['WindowsSdkDir'])
    sdk_version = env['WindowsSDKVersion'].rstrip('\\/')
    vc = Path(env['VCToolsInstallDir'])
    files = [src / 'buildtools/win/gn.exe',
             src / 'third_party/llvm-build/Release+Asserts/bin/clang-cl.exe',
             src / 'third_party/llvm-build/Release+Asserts/bin/lld-link.exe',
             depot / 'gn.bat', depot / 'gn.py', depot / 'autoninja.bat', depot / 'autoninja.py',
             depot / 'bootstrap/manifest.txt', depot / 'bootstrap/win_tools.bat',
             depot / 'bootstrap/bootstrap.py', depot / 'python3_bin_reldir.txt',
             sdk / f'Include/{sdk_version}/um/Windows.h',
             sdk / f'Include/{sdk_version}/ucrt/stdio.h',
             sdk / f'Lib/{sdk_version}/um/x64/kernel32.lib',
             sdk / f'Lib/{sdk_version}/um/x64/d3d12.lib',
             sdk / f'Lib/{sdk_version}/um/x64/dxgi.lib',
             sdk / f'Lib/{sdk_version}/ucrt/x64/ucrt.lib',
             vc / 'include/vcruntime.h', vc / 'lib/x64/vcruntime.lib']
    python_dir = Path((depot / 'python3_bin_reldir.txt').read_text(encoding='utf-8').strip())
    if python_dir.is_absolute() or '..' in python_dir.parts:
        raise ValueError('depot Python pointer escapes the selected tool installation')
    files.append(depot / python_dir / 'python3.exe')
    files.extend(path for path in (src / 'third_party/siso/cipd/siso.exe',
                                  src / 'third_party/ninja/ninja.exe') if path.is_file())
    return [{'path': str(file), 'sha256': digest(file)} for file in files]


def batch_command(script: Path, args: list[str]) -> str:
    # All shell syntax belongs to this function, never to path or CLI values.
    # GN arguments are written to args.gn, avoiding nested cmd/GN string quoting.
    words = [str(script), *args]
    if any(re.search(r'[%!&|<>^"\r\n]', word) for word in words):
        raise ValueError('batch paths and arguments cannot contain cmd metacharacters')
    command = '"' + ' '.join('"' + word + '"' for word in words) + '"'
    return '"' + os.environ.get('COMSPEC', r'C:\Windows\System32\cmd.exe') + '" /d /s /c ' + command


def developer_environment(vs: Path) -> dict[str, str]:
    script = vs / 'VC/Auxiliary/Build/vcvars64.bat'
    batch_command(script, [])  # Reject expansion before constructing fixed shell syntax.
    command = f'""{script}" >nul && set"'
    result = subprocess.check_output('"' + os.environ.get('COMSPEC', r'C:\Windows\System32\cmd.exe')
                                     + '" /d /s /c ' + command, text=True)
    env = dict(os.environ)
    for line in result.splitlines():
        key, sep, value = line.partition('=')
        if sep and key:
            for existing in list(env):
                if existing.casefold() == key.casefold():
                    del env[existing]
            env[key] = value
    return env


def copy_runtime(native: Path, out: Path) -> list[dict[str, str]]:
    # Check the entire provider closure before copying any of it.
    files = [native / name for name in RUNTIME_FILES]
    for file in files:
        if not file.is_file():
            raise ValueError(f'missing built provider dependency: {file}')
    records = []
    for file in files:
        target = out / file.name
        shutil.copy2(file, target)
        records.append({'path': str(target), 'sha256': digest(target)})
    return records


def require_long_paths() -> int:
    """Require Windows long paths before Chromium generates long filenames."""
    import winreg
    key = r'SYSTEM\CurrentControlSet\Control\FileSystem'
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as registry:
        value, _ = winreg.QueryValueEx(registry, 'LongPathsEnabled')
    if value != 1:
        raise ValueError('Windows LongPathsEnabled must be 1 before building Chromium; '
                         'back up the FileSystem registry key before enabling it')
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--src', type=Path, required=True, help='pinned Chromium src checkout')
    parser.add_argument('--depot-tools', type=Path, required=True, help='pinned depot_tools checkout')
    parser.add_argument('--vs-install', type=Path, required=True, help='installed Visual Studio Build Tools root')
    parser.add_argument('--vs-version', choices=('2026', '2022'), default='2026', help='installed Visual Studio toolchain version')
    parser.add_argument('--dxc-root', type=Path, required=True, help='official DXC package with inc and bin/x64')
    parser.add_argument('--native-out', type=Path, required=True, help='fresh C DLL build output')
    parser.add_argument('--out', default='out/fawn_release', help='build directory beneath src/out')
    parser.add_argument('--jobs', type=int, default=4, help='positive local build parallelism')
    parser.add_argument('--browser-only', action='store_true',
                        help='build Chromium alone; no provider build or integration qualification')
    parser.add_argument('--skip-hooks', action='store_true', help='explicitly reuse completed Chromium hooks')
    parser.add_argument('--check', action='store_true', help='check pinned source and both patches without applying')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires actual Windows; no cross-compile or host fallback')
    src = args.src.resolve()
    out = (src / args.out).resolve()
    if '..' in Path(args.out).parts or src / 'out' not in out.parents or args.jobs < 1:
        parser.error('out must be beneath Chromium src/out and jobs must be positive')
    pending = patch_plan(src)
    if args.check:
        print(json.dumps({'pins': source_pins(), 'patchesPending': len(pending)}))
        return 0
    long_paths = require_long_paths()
    native = args.native_out.resolve()
    if not args.browser_only and native.exists() and any(native.iterdir()):
        parser.error('native-out must be fresh; retain previous producer evidence separately')
    out.mkdir(parents=True, exist_ok=True)
    receipt = {'schemaVersion': 1, 'kind': 'windows-chromium-diagnostic-build',
               'chromiumRevision': source_pins()[0], 'dawnRevision': source_pins()[1],
               'builderSha256': digest(Path(__file__)), 'python': sys.version, 'platform': sys.platform,
               'vsInstall': str(args.vs_install.resolve()), 'vsVersion': args.vs_version,
               'longPathsEnabled': long_paths,
               'patches': {file.name: digest(file) for file in (LANE / 'patches').glob('*-doe-runtime.patch')},
               'scope': 'browser-only' if args.browser_only else 'browser-and-current-c-provider',
               'providerBuilt': False, 'commands': [], 'completed': False,
               'sourceBefore': source_diffs(src, out, 'before')}
    receipt_file = out / ('fawn-windows-browser-build.json' if args.browser_only else 'fawn-windows-build.json')
    env = developer_environment(args.vs_install.resolve())
    key = next(key for key in env if key.casefold() == 'path')
    env[key] = str(args.depot_tools.resolve()) + os.pathsep + env[key]
    env.update(DEPOT_TOOLS_WIN_TOOLCHAIN='0', DEPOT_TOOLS_UPDATE='0', GYP_MSVS_VERSION=args.vs_version,
               **{f'vs{args.vs_version}_install': str(args.vs_install.resolve())})
    receipt['windowsSdkVersion'] = env.get('WindowsSDKVersion')
    receipt['vcToolsVersion'] = env.get('VCToolsVersion')

    def run(command: list[str] | str, cwd: Path = src) -> None:
        receipt['commands'].append({'command': command, 'cwd': str(cwd)})
        receipt_file.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
        print(command if isinstance(command, str) else subprocess.list2cmdline(command), flush=True)
        subprocess.run(command, cwd=cwd, env=env, check=True)

    try:
        for root, patch in pending:
            run(['git', '-c', 'core.autocrlf=false', '-C', str(root), 'apply', str(patch)])
        receipt['sourcePatched'] = source_diffs(src, out, 'patched')
        if not (args.depot_tools / 'python3_bin_reldir.txt').is_file():
            run(batch_command(args.depot_tools.absolute() / 'bootstrap/win_tools.bat', []))
        if not args.skip_hooks:
            run(batch_command(args.depot_tools.resolve() / 'gclient.bat', ['runhooks']))
        clang = src / 'third_party/llvm-build/Release+Asserts/bin/clang-cl.exe'
        if not clang.is_file():
            raise ValueError(f'missing Chromium clang-cl after hooks: {clang}')
        receipt['clangVersion'] = subprocess.check_output([str(clang), '--version'], text=True).strip()
        receipt['depotToolsRevision'] = subprocess.check_output(['git', '-C', str(args.depot_tools),
                                                              'rev-parse', 'HEAD'], text=True).strip()
        receipt['selectedToolAndSdkInputs'] = tool_inputs(src, args.depot_tools, env)
        if not args.browser_only:
            run([sys.executable, str(REPO / 'runtime/c/native/tools/build_windows.py'), '--out', str(native),
                 '--dxc-root', str(args.dxc_root.resolve()), '--clang-cl', str(clang)], REPO)
            receipt['nativeScopeSha256'] = digest(native / 'native-scope.json')
            receipt['providerBuilt'] = True
        (out / 'args.gn').write_text(RELEASE_ARGS, encoding='utf-8', newline='\n')
        receipt['argsSha256'] = digest(out / 'args.gn')
        run(batch_command(args.depot_tools.resolve() / 'gn.bat', ['gen', str(out)]))
        run(batch_command(args.depot_tools.resolve() / 'autoninja.bat', ['-C', str(out), 'chrome', '-j', str(args.jobs)]))
        if not args.browser_only:
            receipt['runtimeArtifacts'] = copy_runtime(native, out)
        for name in ('chrome.exe', 'chrome.dll', 'icudtl.dat', 'resources.pak', 'v8_context_snapshot.bin'):
            file = out / name
            if not file.is_file():
                raise ValueError(f'incomplete Windows browser build: {file}')
        receipt['browserArtifacts'] = [{'path': str(out / name), 'sha256': digest(out / name)}
                                     for name in ('chrome.exe', 'chrome.dll')]
        receipt['selectedToolAndSdkInputsAfter'] = tool_inputs(src, args.depot_tools, env)
        if receipt['selectedToolAndSdkInputsAfter'] != receipt['selectedToolAndSdkInputs']:
            raise ValueError('selected tool or SDK input changed during build')
        receipt['sourceAfter'] = source_diffs(src, out, 'after')
        if [x['sha256'] for x in receipt['sourceAfter']] != [x['sha256'] for x in receipt['sourcePatched']]:
            raise ValueError('tracked Chromium or Dawn source changed during build')
        receipt['completed'] = True
        return 0
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        try:
            receipt['sourceAtExit'] = source_diffs(src, out, 'exit')
        except (OSError, subprocess.SubprocessError) as error:
            receipt['sourceAtExitError'] = str(error)
            receipt['completed'] = False
            if 'error' not in receipt:
                receipt_file.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
                raise
        receipt_file.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    raise SystemExit(main())
