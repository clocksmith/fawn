#!/usr/bin/env python3
"""Sealed source-only M10 native suite. Default operation verifies sources only.
Candidate compilation/execution requires --evaluate-candidate, --repository and
an explicit immutable --candidate commit. Never pass a moving worktree as source.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import subprocess
import time

HERE = Path(__file__).resolve().parent
FREEZE = '8530b04f6b2171bc24888530dd200e2f4a60983f'
CFLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-Wvla',
          '-Wno-unused-const-variable', '-Wno-misleading-indentation',
          '-fno-fast-math', '-ffp-contract=off', '-frounding-math']
CPPFLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fno-exceptions', '-fno-rtti']
ALLOCATORS = ['malloc', 'calloc', 'realloc', 'free', 'aligned_alloc', 'posix_memalign']
FENV = ['fegetenv', 'feholdexcept', 'fesetenv', 'feupdateenv', 'fesetround', 'fegetround', 'feclearexcept', 'fesetexceptflag']
EXPORTS = {'doe_compiler_compile', 'doe_compiler_workspace_requirements', 'doe_compiler_compile_v2',
           'doe_compiler_job_requirements', 'doe_compiler_compile_job',
           'doe_compiler_typed_requirements', 'doe_compiler_compile_typed'}
C_TESTS = ['abi_m10.c', 'capacity_m10.c', 'host_m10.c', 'legacy_m10.c']


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, data: object) -> None:
    path.write_text(json.dumps(data, indent=2, sort_keys=True)+'\n', encoding='utf-8')


def verify_seal() -> dict[str, str]:
    entries = {}
    for line in (HERE/'SOURCE_SEAL.sha256').read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        path = HERE/name
        if path.resolve().parent != HERE and HERE not in path.resolve().parents:
            raise RuntimeError('invalid seal path')
        if sha(path.read_bytes()) != digest:
            raise RuntimeError('evaluator seal differs: '+name)
        entries[name] = digest
    if not entries:
        raise RuntimeError('empty evaluator source seal')
    return entries


class Receipt:
    def __init__(self, output: Path):
        if output.exists():
            raise RuntimeError('use a fresh absent output directory; old evidence is immutable')
        output.mkdir(parents=True)
        self.output = output
        self.commands = []
        self.failures = []

    def run(self, label: str, args: list[str | Path], timeout: int = 240, required: bool = True) -> str | None:
        started = time.monotonic()
        command = list(map(str, args))
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        try:
            p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, check=False, env=env)
            code, text = p.returncode, p.stdout.decode('utf-8', errors='replace')
        except subprocess.TimeoutExpired as e:
            code, text = 124, (e.stdout or b'').decode('utf-8', errors='replace')+'\nTIMEOUT\n'
        except OSError as e:
            code, text = 127, str(e)
        row = {'label': label, 'command': command, 'returncode': code, 'elapsed_seconds': time.monotonic()-started}
        self.commands.append(row)
        (self.output/(label+'.log')).write_text(text, encoding='utf-8')
        write_json(self.output/'commands.json', self.commands)
        if code:
            self.failures.append(row)
            if not (self.output/'FIRST_FAILURE.json').exists():
                write_json(self.output/'FIRST_FAILURE.json', row)
            if required:
                raise RuntimeError(label+' failed; original output retained')
            return None
        return text


def source_snapshot(repository: Path, commit: str, output: Path, receipt: Receipt) -> tuple[Path, dict[str, str]]:
    if not re.fullmatch('[0-9a-f]{40}', commit):
        raise RuntimeError('candidate must be a full fixed 40-hex commit')
    resolved = subprocess.check_output(['git', '-C', str(repository), 'rev-parse', '--verify', commit+'^{commit}'], text=True).strip()
    if resolved != commit:
        raise RuntimeError('candidate commit identity differs')
    prefix = 'runtime/c/compiler/'
    files = subprocess.check_output(['git', '-C', str(repository), 'ls-tree', '-r', '--name-only', commit, prefix+'src', prefix+'include'], text=True).splitlines()
    owner = output/'candidate-source'
    identities = {}
    for name in files:
        relative = name.removeprefix(prefix)
        if '..' in Path(relative).parts or name == relative:
            raise RuntimeError('unexpected compiler source path')
        data = subprocess.check_output(['git', '-C', str(repository), 'show', commit+':'+name])
        path = owner/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        identities[name] = sha(data)
    for header in (HERE/'frozen_headers').glob('*.h'):
        if (owner/'include'/header.name).read_bytes() != header.read_bytes():
            raise RuntimeError('frozen public header changed: '+header.name)
    write_json(receipt.output/'candidate-source-identities.json', {'commit': commit, 'files': identities})
    return owner, identities


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--syntax-only', action='store_true', help='Compile independent test syntax only against sealed headers, never candidate sources')
    ap.add_argument('--evaluate-candidate', action='store_true', help='Explicit evaluator handoff authorization is still required')
    ap.add_argument('--repository', type=Path)
    ap.add_argument('--candidate')
    ap.add_argument('--out', type=Path, help='Fresh absent receipt directory')
    ap.add_argument('--build-dir', type=Path, help='Fresh absent directory for future source snapshots and binaries, preferably task-private /tmp')
    ap.add_argument('--cc', default='cc')
    ap.add_argument('--cxx', default='c++')
    ap.add_argument('--sanitize', action='store_true', help='Additional ASan/UBSan source build and consumers, not a substitute for normal routes')
    args = ap.parse_args()
    if args.syntax_only and args.evaluate_candidate:
        ap.error('choose syntax-only or candidate evaluation, never both')
    entries = verify_seal()
    if not args.syntax_only and not args.evaluate_candidate:
        print(json.dumps({'status': 'SOURCE_SEAL_VERIFIED', 'files': len(entries), 'candidate_inspected_or_run': False}))
        return 0
    if not args.out:
        ap.error('--out is required for recorded commands')
    if args.evaluate_candidate and (not args.repository or not args.candidate):
        ap.error('candidate evaluation requires --repository and --candidate')
    r = Receipt(args.out.resolve())
    report = {'status': 'FAIL', 'scope': 'native ABI and host only; no semantic/GPU/full migration acceptance',
              'freeze_commit': FREEZE, 'source_seal_sha256': sha((HERE/'SOURCE_SEAL.sha256').read_bytes()),
              'candidate_inspected_or_run': False, 'sanitizer': 'NOT_RUN'}
    try:
        report['cc'] = r.run('cc-version', [args.cc, '--version']).splitlines()[0]
        report['cxx'] = r.run('cxx-version', [args.cxx, '--version']).splitlines()[0]
        include = '-I'+str(HERE/'frozen_headers')
        for name in C_TESTS+['faults_m10.c', 'allocator_m10.c']:
            r.run('syntax-'+Path(name).stem, [args.cc, *CFLAGS, include, '-fsyntax-only', HERE/name])
        r.run('syntax-cpp', [args.cxx, *CPPFLAGS, include, '-fsyntax-only', HERE/'consumer_m10.cpp'])
        r.run('syntax-allocator-wraps', [args.cc, *CFLAGS, include, '-DWRAP_ALLOCATORS', '-fsyntax-only', HERE/'allocator_m10.c'])
        if args.syntax_only:
            report['status'] = 'SYNTAX_ONLY_PASS'
            report['claim'] = 'Only independent test syntax checked. No candidate inspected, linked, loaded, or executed.'
        else:
            report['candidate_inspected_or_run'] = True
            build_root = r.output
            if args.build_dir:
                build_root = args.build_dir.resolve()
                if build_root.exists():
                    raise RuntimeError('use a fresh absent build directory; never overwrite old artifacts')
                build_root.mkdir(parents=True)
            report['build_directory'] = str(build_root)
            owner, identities = source_snapshot(args.repository.resolve(), args.candidate, build_root, r)
            report['candidate_commit'] = args.candidate
            build = build_root/'build';build.mkdir()
            sources = sorted(p for p in (owner/'src').rglob('*.c') if not p.name.startswith('cli'))
            if not sources:
                raise RuntimeError('no library C sources')
            objects = []
            for i, source in enumerate(sources):
                obj = build/(str(i)+'-'+source.stem+'.o')
                r.run('build-object-'+str(i), [args.cc, *CFLAGS, '-fPIC', '-fstack-usage', '-I'+str(owner/'include'), '-I'+str(owner/'src'), '-c', source, '-o', obj])
                objects.append(obj)
            archive, shared = build/'libdoe-native.a', build/'libdoe-native.so'
            r.run('archive', ['ar', 'rcs', archive, *objects])
            r.run('shared', [args.cc, '-shared', *objects, '-Wl,--version-script='+str(owner/'src/exports.map'), '-lm', '-o', shared])
            report['source_built_artifacts'] = {p.name: sha(p.read_bytes()) for p in (archive, shared)}
            imports = r.run('archive-imports', ['nm', '-u', archive])
            import_names = {line.split()[-1].split('@')[0] for line in imports.splitlines() if line.split()}
            report['allocator_imports'] = sorted(import_names & set(ALLOCATORS))
            exports = r.run('shared-exports', ['nm', '-D', '--defined-only', shared])
            exported = {line.split()[-1].split('@')[0] for line in exports.splitlines() if len(line.split()) >= 3 and line.split()[-2].upper() in {'T','W'}}
            report['exports_exact'] = exported == EXPORTS
            r.run('shared-needed', ['readelf', '-d', shared])
            if report['allocator_imports'] or not report['exports_exact']:
                raise RuntimeError('allocator import or public export contract failed')
            wrappers = ['-Wl,'+','.join('--wrap='+n for n in ALLOCATORS)]
            for mode, link in [('archive', [archive]), ('shared', [shared, '-Wl,-rpath,'+str(build)])]:
                for source in C_TESTS:
                    name = mode+'-'+Path(source).stem
                    binary = build/name
                    extra = ['-DWRAP_ALLOCATORS', *wrappers] if mode == 'archive' else []
                    if r.run('build-'+name, [args.cc, *CFLAGS, include, HERE/source, HERE/'allocator_m10.c', *link, *extra, '-pthread', '-lm', '-o', binary], required=False) is not None:
                        r.run('run-'+name, [binary], required=False)
                binary = build/(mode+'-cpp')
                if r.run('build-'+mode+'-cpp', [args.cxx, *CPPFLAGS, include, HERE/'consumer_m10.cpp', *link, '-lm', '-o', binary], required=False) is not None:
                    r.run('run-'+mode+'-cpp', [binary], required=False)
            binary = build/'faults'
            host_fenv = FENV + (['feenableexcept', 'fedisableexcept'] if platform.libc_ver()[0] == 'glibc' else [])
            fwrap = ['-Wl,'+','.join('--wrap='+n for n in host_fenv)]
            if r.run('build-faults', [args.cc, *CFLAGS, include, HERE/'faults_m10.c', HERE/'allocator_m10.c', archive, '-DWRAP_ALLOCATORS', *wrappers, *fwrap, '-lm', '-o', binary], required=False) is not None:
                r.run('run-faults', [binary], required=False)
            if args.sanitize:
                san = ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
                lib = build/'libdoe-sanitized.so'
                r.run('build-sanitized-library', [args.cc, *CFLAGS, *san, '-fPIC', '-shared', '-I'+str(owner/'include'), '-I'+str(owner/'src'), *sources, '-Wl,--version-script='+str(owner/'src/exports.map'), '-lm', '-o', lib])
                for source in C_TESTS:
                    name='sanitized-'+Path(source).stem;binary=build/name
                    if r.run('build-'+name, [args.cc, *CFLAGS, *san, include, HERE/source, HERE/'allocator_m10.c', lib, '-Wl,-rpath,'+str(build), '-pthread', '-lm', '-o', binary], required=False) is not None:
                        r.run('run-'+name, [binary], required=False)
                report['sanitizer'] = 'RUN; individual outcomes in commands'
            report['source_unchanged_during_run'] = all(sha((owner/p.removeprefix('runtime/c/compiler/')).read_bytes()) == d for p,d in identities.items())
            if not report['source_unchanged_during_run']:
                raise RuntimeError('candidate source snapshot modified during evaluation')
            report['status'] = 'PASS' if not r.failures else 'FAIL'
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as e:
        report['error'] = str(e)
    report['failures'] = r.failures
    write_json(r.output/'REPORT.json', report)
    print(json.dumps(report, indent=2, sort_keys=True))
    return int(report['status'] not in {'PASS','SYNTAX_ONLY_PASS'})


if __name__ == '__main__':
    raise SystemExit(main())
