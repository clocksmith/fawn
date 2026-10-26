#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build and exercise the x64 Windows DXC process boundary, including real DXC.

Run in a Windows SDK developer environment. Output must be fresh; source hashes,
commands, real compiler diagnostics and DXIL artifacts remain in that directory.
No shader adapter, D3D12 execution or release qualification is implied.
"""
from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--dxc', required=True, type=Path)
    parser.add_argument('--cc', default='clang-cl')
    parser.add_argument('--worker', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('requires Windows and its real SDK')
    if not args.worker:
        # Native ctypes calls release the GIL and can deadlock inside a broken
        # runner. Bound the whole worker, including all owned child processes.
        process = subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                                    *sys.argv[1:], '--worker'])
        try:
            code = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                           check=True, timeout=30)
            process.wait(timeout=30)
            raise TimeoutError('Windows DXC process controls exceeded their deadline')
        if code != 0:
            raise subprocess.CalledProcessError(code, process.args)
        return
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        parser.error('requires a 64-bit Python host for the x64 Windows control')
    compiler = shutil.which(args.cc)
    if compiler is None or not args.dxc.is_file():
        parser.error('requires clang-cl and an existing official DXC executable')
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    if any(out.iterdir()):
        parser.error('out must be fresh or empty')
    sources = [ROOT / 'src/dxil' / name for name in
               ['dxil_toolchain.c', 'dxil_process_windows.c', 'dxil_mapping.c',
                'dxil_spec.c', 'dxil_validate.c']]
    helper = ROOT / 'src/dxil/tests/dxc_process_probe.c'
    dxc = args.dxc.resolve()
    bound = sources + [helper, Path(__file__), *sorted((ROOT / 'src/dxil').glob('*.h'))]
    identity = {
        'scope': 'Windows DXC process boundary; no D3D12/device qualification',
        'host': socket.gethostname(), 'platform': platform.platform(),
        'compiler': compiler,
        'compiler_sha256': hashlib.sha256(Path(compiler).read_bytes()).hexdigest(),
        'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                                 cwd=ROOT, text=True).strip(),
        'source_status': subprocess.check_output(['git', 'status', '--porcelain', '--',
                                                  *(str(path) for path in bound)],
                                                 cwd=ROOT, text=True),
        'sha256': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in bound},
        'dxc_sha256': hashlib.sha256(dxc.read_bytes()).hexdigest(),
        'dxc_dll_sha256': {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                           for path in sorted(dxc.parent.glob('dx*.dll'))},
    }
    (out / 'inputs.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    commands: list[list[str]] = []

    def invoke(command: list[str], **options: object) -> subprocess.CompletedProcess[str]:
        commands.append(command)
        result = subprocess.run(command, cwd=out, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=60,
                                **options)
        (out / f'command-{len(commands):02}.log').write_text(
            subprocess.list2cmdline(command) + '\n' + result.stdout + result.stderr,
            encoding='utf-8')
        result.check_returncode()
        return result

    strict = [compiler, '--target=x86_64-pc-windows-msvc', '/nologo', '/MD', '/O2', '/WX',
              '/D_CRT_SECURE_NO_WARNINGS', '/clang:-std=c11', '/clang:-Wall',
              '/clang:-Wextra', '/clang:-Wpedantic', '/clang:-Wconversion',
              '/clang:-Wshadow', '/clang:-Wstrict-prototypes',
              '/clang:-Wmissing-prototypes', '/clang:-Wswitch-enum']
    library = out / 'dxil_process.dll'
    invoke(strict + ['/LD', *(str(source) for source in sources),
                     '/Fe' + str(library), '/link', '/EXPORT:dxil_dxc_run',
                     '/EXPORT:dxil_validate_output'])
    # Spaces, Unicode and shell metacharacters in the executable and shader paths.
    directory = out / 'space & \u96ea'
    directory.mkdir()
    probe = directory / 'probe compiler.exe'
    invoke(strict + [str(helper), '/Fe' + str(probe)])
    dll = ctypes.CDLL(str(library))
    runner = dll.dxil_dxc_run
    runner.argtypes = [ctypes.c_char_p] * 5 + [ctypes.c_int, ctypes.c_void_p, ctypes.c_size_t]
    runner.restype = ctypes.c_int
    validate = dll.dxil_validate_output
    validate.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_char_p,
                        ctypes.c_void_p, ctypes.c_size_t]
    validate.restype = ctypes.c_int
    input_path = directory / 'input shader.hlsl'
    output_path = directory / 'output shader.dxil'
    input_path.write_text('[numthreads(1,1,1)] void main() {}\n', encoding='utf-8')
    checks: list[dict[str, object]] = []

    def run(executable: Path | str, profile: str, entry: str = 'main',
            discovery: int = 0, capacity: int = 2048) -> tuple[int, str]:
        error = ctypes.create_string_buffer(max(capacity, 1))
        if capacity > 1:
            error.value = b'untouched'
        status = runner(*(str(value).encode('utf-8') for value in
                          [executable, profile, entry, output_path, input_path]),
                        discovery, error if capacity else None, capacity)
        detail = error.value.decode('utf-8', errors='replace')
        checks.append({'executable': str(executable), 'profile': profile,
                       'discovery': discovery, 'status': status, 'detail': detail})
        with (out / 'checks.jsonl').open('a', encoding='utf-8') as log:
            log.write(json.dumps(checks[-1], ensure_ascii=True) + '\n')
        print(json.dumps(checks[-1], ensure_ascii=True), flush=True)
        return status, detail

    assert run(probe, 'success', 'entry "quoted" \u96ea\\') == (0, 'untouched')
    assert output_path.read_bytes() == b'exact arguments'
    for mode, code, detail in [('stderr', 3, 'compiler failed'),
                               ('stdout', 4, 'stdout fallback'),
                               ('noisy', 5, 'bounded stderr'),
                               ('crash', 3221225477, '')]:
        status, message = run(probe, mode)
        assert status == 4 and f'exit code {code}' in message, message
        assert detail in message and 'profile' in message and 'main' in message
        if mode == 'stderr':
            assert 'stdout must not win' not in message
    assert run(probe, 'stderr', capacity=1) == (4, '')
    assert run(probe, 'stderr', capacity=0) == (4, '')
    for discovery, label in enumerate(['explicit', 'DOE_WGSL_DXC', 'PATH', 'PATH']):
        status, message = run(directory / 'missing.exe', 'cs_6_0', discovery=discovery)
        assert status == 6 and label in message and 'not found' in message, message
    # Reject malformed UTF-8 and oversized command lines before launching.
    for entry in [b'\xff', b'x' * 40000]:
        error = ctypes.create_string_buffer(2048)
        assert runner(str(probe).encode('utf-8'), b'success', entry,
                      str(output_path).encode('utf-8'), str(input_path).encode('utf-8'),
                      0, error, len(error)) == 6
        assert b'Windows limit' in error.value
    not_executable = directory / 'not executable.exe'
    not_executable.write_text('not a Windows executable', encoding='utf-8')
    status, message = run(not_executable, 'cs_6_0')
    assert status == 6 and 'Windows error' in message, message
    # Only the three intended standard handles may enter the child.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    class Security(ctypes.Structure):
        _fields_ = [('length', wintypes.DWORD), ('descriptor', ctypes.c_void_p),
                    ('inherit', wintypes.BOOL)]
    kernel.CreateEventW.argtypes = [ctypes.POINTER(Security), wintypes.BOOL,
                                   wintypes.BOOL, wintypes.LPCWSTR]
    kernel.CreateEventW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.GetCurrentProcess.restype = wintypes.HANDLE
    kernel.GetProcessHandleCount.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.ResetEvent.argtypes = [wintypes.HANDLE]
    security = Security(ctypes.sizeof(Security), None, True)
    unrelated = kernel.CreateEventW(ctypes.byref(security), True, False, None)
    assert unrelated
    try:
        assert run(probe, 'handles', str(unrelated))[0] == 0
        assert kernel.WaitForSingleObject(unrelated, 0) == 258
        # Positive control proves the event probe detects an actually inherited
        # parent object, rather than accepting every child-local handle value.
        startup = subprocess.STARTUPINFO()
        startup.lpAttributeList = {'handle_list': [unrelated]}
        invoke([str(probe), '-T', 'handles', '-E', str(unrelated),
                '-Fo', str(output_path), str(input_path)],
               startupinfo=startup, close_fds=True)
        assert kernel.WaitForSingleObject(unrelated, 0) == 0
        assert kernel.ResetEvent(unrelated)
    finally:
        assert kernel.CloseHandle(unrelated)
    before, after = wintypes.DWORD(), wintypes.DWORD()
    assert kernel.GetProcessHandleCount(kernel.GetCurrentProcess(), ctypes.byref(before))
    for _ in range(12):
        assert run(probe, 'stderr')[0] == 4
        assert run(directory / 'missing.exe', 'cs_6_0')[0] == 6
    assert kernel.GetProcessHandleCount(kernel.GetCurrentProcess(), ctypes.byref(after))
    assert before.value == after.value, (before.value, after.value)
    # Bare executable lookup also traverses CreateProcessW, without cmd.exe.
    previous_path = os.environ.get('PATH', '')
    os.environ['PATH'] = str(directory) + os.pathsep + previous_path
    try:
        assert run(probe.name, 'success', 'entry "quoted" \u96ea\\', discovery=2)[0] == 0
    finally:
        os.environ['PATH'] = previous_path
    output_path.unlink()
    input_path.write_text(
        'RWStructuredBuffer<uint> result : register(u0);\n'
        '[numthreads(1,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n'
        '    result[id.x] = id.x + 17u;\n}\n', encoding='utf-8')
    (out / 'valid.hlsl').write_bytes(input_path.read_bytes())
    assert run(dxc, 'cs_6_0')[0] == 0
    dxil = output_path.read_bytes()
    assert dxil.startswith(b'DXBC')
    error = ctypes.create_string_buffer(2048)
    assert validate(dxil, len(dxil), b'DXC', error, len(error)) == 0, error.value
    invoke([str(dxc), '-dumpbin', str(output_path)])
    input_path.write_text('this is invalid HLSL;\n', encoding='utf-8')
    (out / 'invalid.hlsl').write_bytes(input_path.read_bytes())
    status, message = run(dxc, 'cs_6_0')
    assert status == 4 and 'error' in message.lower(), message
    assert identity['sha256'] == {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in bound}, 'source changed during the Windows control'
    summary = {**identity, 'commands': commands, 'checks': checks,
               'handle_count': after.value}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('PASS: Windows process controls and real DXC compilation/validation', flush=True)


if __name__ == '__main__':
    main()
