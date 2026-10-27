#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Stage a diagnostic Windows Fawn bundle using the pinned Chromium owner.

This consumes completed source-bound builds. It does not build an installer,
qualify browser behavior, or inherit release qualification from file copying.
"""
from __future__ import annotations

import argparse
from contextlib import chdir
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

LANE = Path(__file__).resolve().parents[1]
TARGET = '//chrome/installer/mini_installer:mini_installer_archive'


def digest(path: Path) -> str:
    state = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            state.update(block)
    return state.hexdigest()


def checked_file(path: Path, expected: str) -> None:
    if path.is_symlink() or not path.is_file() or digest(path) != expected:
        raise ValueError(f'missing, redirected or changed input: {path}')


def upstream_action_arguments(command: list[str], src: Path, env: dict[str, str],
                              output: Path, timeout: float = 60) -> list[str]:
    def preserve(stdout: bytes | None, stderr: bytes | None) -> None:
        (output / 'upstream-action-args.json').write_bytes(stdout or b'')
        (output / 'upstream-action-args.stderr').write_bytes(stderr or b'')

    try:
        result = subprocess.run(command, cwd=src, env=env, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired as error:
        preserve(error.stdout, error.stderr)
        raise
    preserve(result.stdout, result.stderr)
    result.check_returncode()
    arguments = json.loads(result.stdout)[TARGET]['args']
    if not isinstance(arguments, list) or not all(isinstance(item, str) for item in arguments):
        raise ValueError('GN must supply the canonical installer action argument list')
    return arguments


def validate_producers(browser: Path, native: Path, launcher: Path) -> tuple[dict, dict, dict]:
    browser_record = json.loads((browser / 'fawn-windows-browser-build.json').read_text())
    if not browser_record.get('completed'):
        raise ValueError('the actual Windows Chromium build must be complete')
    if {Path(item['path']).name for item in browser_record['browserArtifacts']} != {'chrome.exe', 'chrome.dll'}:
        raise ValueError('the browser producer must bind both executable and implementation image')
    for item in browser_record['browserArtifacts']:
        actual = browser / Path(item['path']).name
        if not actual.samefile(item['path']):
            raise ValueError('browser artifact is outside the selected build')
        checked_file(actual, item['sha256'])
    checked_file(browser / 'args.gn', browser_record['argsSha256'])
    if browser_record['selectedToolAndSdkInputsAfter'] != browser_record['selectedToolAndSdkInputs']:
        raise ValueError('the browser producer recorded toolchain drift')
    for item in browser_record['selectedToolAndSdkInputs']:
        checked_file(Path(item['path']), item['sha256'])

    native_record = json.loads((native / 'MANIFEST.json').read_text())
    for name, expected in native_record['artifacts'].items():
        path = native / name
        if not path.resolve().is_relative_to(native.resolve()):
            raise ValueError('native artifact escapes its source package')
        checked_file(path, expected)
    launcher_record = json.loads((launcher.parent / 'launcher-build.json').read_text())
    if not launcher_record.get('completed') or launcher_record.get('privateRuntimeRelativeDirectory') != 'lib/doe':
        raise ValueError('the launcher must admit the private lib/doe layout')
    sources = [value for name, value in launcher_record['inputs'].items()
               if Path(name).name == 'fawn_windows.c']
    if sources != [digest(LANE / 'launcher/fawn_windows.c')]:
        raise ValueError('launcher source differs from this staging contract')
    checked_file(launcher, launcher_record['launcherSha256'])
    return browser_record, native_record, launcher_record


def stage(src: Path, browser: Path, native: Path, launcher: Path, output: Path) -> dict:
    if os.name != 'nt':
        raise ValueError('requires actual Windows; no platform or compiler fallback')
    if output.exists() or output.is_symlink():
        raise FileExistsError('use a fresh staging directory; existing evidence is retained')
    output = output.resolve()
    if output.is_relative_to(src) or src.is_relative_to(output):
        raise ValueError('staging and Chromium source directories must be disjoint')
    browser_record, native_record, launcher_record = validate_producers(browser, native, launcher)
    build = runpy.run_path(str(LANE / 'scripts/build-windows.py'))
    pins = dict(zip(('chromium', 'dawn'), build['source_pins']()))
    for repository, key in ((src, 'chromium'), (src / 'third_party/dawn', 'dawn')):
        actual = subprocess.check_output(['git', '-C', str(repository), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != pins[key] or actual != browser_record[key + 'Revision']:
            raise ValueError(f'producer revision differs from the selected source: {key}')
    files = build['RUNTIME_FILES']
    for name in files:
        checked_file(native / 'bin' / name, native_record['artifacts']['bin/' + name])
    output.mkdir(parents=True)
    record = {'kind': 'windows-fawn-diagnostic-stage', 'completed': False,
              'scope': 'canonical Chromium staging plus private C dependencies; no installer, browser, performance or release qualification',
              'browserBuild': browser_record, 'cPackage': native_record, 'launcherBuild': launcher_record,
              'stagerSha256': digest(Path(__file__)), 'commands': []}
    report = output / 'STAGE.json'

    def save() -> None:
        report.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')

    save()
    try:
        current = build['source_diffs'](src, output, 'staging-input')
        if [row['sha256'] for row in current] != [row['sha256'] for row in browser_record['sourcePatched']]:
            raise ValueError('Chromium or Dawn tracked source changed after the build')
        env = build['developer_environment'](Path(browser_record['vsInstall']))
        version = browser_record['vsVersion']
        env.update(DEPOT_TOOLS_WIN_TOOLCHAIN='0', DEPOT_TOOLS_UPDATE='0', GYP_MSVS_VERSION=version,
                   **{f'vs{version}_install': browser_record['vsInstall']})
        command = [str(src / 'buildtools/win/gn.exe'), 'desc', str(browser), TARGET, 'args', '--format=json']
        record['commands'].append(command)
        arguments = upstream_action_arguments(command, src, env, output)
        script = src / 'chrome/tools/build/win/create_installer_archive.py'
        spec = importlib.util.spec_from_file_location('fawn_pinned_windows_archive', script)
        if spec is None or spec.loader is None:
            raise ImportError(f'cannot load the canonical Chromium staging owner: {script}')
        upstream = importlib.util.module_from_spec(spec)
        previous_bytecode = sys.dont_write_bytecode
        try:
            sys.dont_write_bytecode = True
            spec.loader.exec_module(upstream)
        finally:
            sys.dont_write_bytecode = previous_bytecode
        original_argv = sys.argv
        with chdir(browser):
            try:
                sys.argv = [str(script), *arguments, '--staging_dir', str(output)]
                upstream.options = upstream._ParseOptions()
            finally:
                sys.argv = original_argv
            options = upstream.options
            if options.component_build == '1':
                raise ValueError('this diagnostic lane requires its canonical noncomponent browser build')
            version = upstream.BuildVersion()
            config = upstream.Readconfig(options.input_file, version)
            upstream.CopyAllFilesToStagingDir(config, options.distribution, str(output), str(browser),
                options.enable_hidpi, options.include_snapshotblob, options.include_dxc,
                options.component_build, options.component_ffmpeg_build, options.verbose)
        package = output / upstream.CHROME_DIR
        upstream_inputs = {name: digest(browser / name) for name in upstream.g_archive_inputs}
        canonical_files = {path.relative_to(package).as_posix(): digest(path)
                           for path in package.rglob('*') if path.is_file()}
        if not set(canonical_files.values()).issubset(set(upstream_inputs.values())):
            raise ValueError('canonical stage contains bytes not bound to an upstream source file')
        for name in ('chrome.exe', f'{version}/chrome.dll', f'{version}/chrome_elf.dll',
                     f'{version}/resources.pak', f'{version}/icudtl.dat', f'{version}/Locales/en-US.pak'):
            if not (package / name).is_file():
                raise ValueError(f'canonical staging omitted a required browser file: {name}')
        dawn_compiler = package / version / 'dxcompiler.dll'
        if options.include_dxc == '1' and not dawn_compiler.is_file():
            raise ValueError('the canonical built-DXC browser dependency is missing')
        closure = package / 'lib/doe'
        closure.mkdir(parents=True)
        for name in files:
            shutil.copy2(native / 'bin' / name, closure / name)
            checked_file(closure / name, native_record['artifacts']['bin/' + name])
        shutil.copy2(native / 'MANIFEST.json', closure / 'source-package-manifest.json')
        shutil.copytree(native / 'licenses', closure / 'licenses')
        shutil.copy2(launcher, package / 'Fawn.exe')
        checked_file(package / 'Fawn.exe', launcher_record['launcherSha256'])
        licenses = package / 'licenses'
        licenses.mkdir(exist_ok=True)
        shutil.copy2(src / 'LICENSE', licenses / 'Chromium.txt')
        shutil.copy2(src / 'third_party/dawn/LICENSE', licenses / 'Dawn.txt')
        if (package / 'dxcompiler.dll').exists():
            raise ValueError('root DXC would make canonical Dawn and private C compiler selection ambiguous')
        if validate_producers(browser, native, launcher) != (browser_record, native_record, launcher_record):
            raise ValueError('producer metadata changed during staging')
        for name, expected in upstream_inputs.items():
            checked_file(browser / name, expected)
        entries = {}
        for path in sorted(package.rglob('*')):
            if path.is_symlink():
                raise ValueError(f'symlinks are not admitted in this diagnostic bundle: {path}')
            if path.is_file():
                entries[path.relative_to(package).as_posix()] = digest(path)
        record.update(packageRoot=str(package), chromiumVersion=version,
                      upstreamOwnerSha256=digest(script),
                      upstreamReleaseFileSha256=digest(src / 'chrome/installer/mini_installer/chrome.release'),
                      upstreamArguments=arguments, upstreamInputsSha256=upstream_inputs,
                      canonicalFilesSha256=canonical_files,
                      files=entries, privateCLibrary='lib/doe/webgpu_doe_c.dll',
                      dawnCompiler=f'{version}/dxcompiler.dll' if options.include_dxc == '1' else None,
                      completed=True)
        return record
    except Exception as error:
        record['failure'] = {'type': type(error).__name__, 'message': str(error)}
        if isinstance(error, subprocess.CalledProcessError):
            record['failure']['returnCode'] = error.returncode
        raise
    finally:
        save()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--src', type=Path, required=True)
    parser.add_argument('--browser-out', type=Path, required=True)
    parser.add_argument('--c-package', type=Path, required=True)
    parser.add_argument('--launcher', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = stage(args.src.resolve(), args.browser_out.resolve(), args.c_package.resolve(),
                   args.launcher.resolve(), args.out.absolute())
    print(json.dumps({'packageRoot': result['packageRoot'], 'files': len(result['files'])}))


if __name__ == '__main__':
    main()
