"""Source-only M8 native/host evaluator; pinned accepted helpers remain unchanged."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

HERE = Path(__file__).resolve().parent
CFLAGS = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Wstrict-prototypes', '-Wmissing-prototypes', '-Werror', '-Wvla', '-fno-fast-math', '-ffp-contract=off', '-frounding-math']
CPPFLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Wconversion', '-Wshadow', '-Werror', '-fno-exceptions', '-fno-rtti']
ALLOCATORS = ['malloc', 'calloc', 'realloc', 'free', 'aligned_alloc', 'posix_memalign']
FENV = ['fegetenv', 'feholdexcept', 'fesetenv', 'feupdateenv', 'fesetround', 'fegetround', 'feclearexcept', 'fesetexceptflag', 'feenableexcept', 'fedisableexcept']
EXPORTS = {'doe_compiler_compile', 'doe_compiler_compile_v2', 'doe_compiler_workspace_requirements', 'doe_compiler_job_requirements', 'doe_compiler_compile_job'}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True)+'\n', encoding='utf-8')


class Stage:
    def __init__(self, output: Path) -> None:
        self.output = output
        self.build = output/'build'
        self.build.mkdir(parents=True, exist_ok=True)
        self.commands: list[dict] = []
        self.failures: list[dict] = []

    def run(self, name: str, command: list[str | Path], required: bool = True, extra_env: dict | None = None) -> str | None:
        started = time.monotonic()
        command = list(map(str, command))
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        env.update(extra_env or {})
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=240, check=False, env=env)
            code, output = result.returncode, result.stdout+result.stderr
        except subprocess.TimeoutExpired as error:
            code, output = 124, str(error)
        (self.output/(name+'.log')).write_text(output, encoding='utf-8')
        row = dict(name=name, command=command, returncode=code, elapsed_seconds=time.monotonic()-started)
        self.commands.append(row)
        save(self.output/'commands.json', self.commands)
        if code:
            self.failures.append(row)
            if not (self.output/'FIRST_FAILURE.json').exists():
                save(self.output/'FIRST_FAILURE.json', row)
            print('FAIL', name, output[-1500:], flush=True)
            if required:
                raise RuntimeError(name+' failed')
            return None
        print('PASS', name, flush=True)
        return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path, help='Pinned candidate compiler owner directory')
    parser.add_argument('--retained-root', required=True, type=Path, help='Repository with unchanged accepted historical tests')
    parser.add_argument('--prebuilt', type=Path, help='Separately supplied prebuilt shared library, never source-build evidence')
    parser.add_argument('--out', required=True, type=Path, help='Fresh output directory preserving earlier attempts')
    parser.add_argument('--skip-sanitizer', action='store_true', help='Explicitly record sanitizer not run')
    args = parser.parse_args()
    owner, repo = args.source_root.resolve(), args.retained_root.resolve()
    if (args.out/'commands.json').exists():
        raise RuntimeError('use a fresh output directory; earlier receipts are immutable')
    stage = Stage(args.out.resolve())
    report: dict = dict(status='FAIL', claim='Linux x86-64 GCC14.2 native compiler host only, not GPU qualification')
    try:
        freeze = json.loads((HERE/'PRE_CANDIDATE_FREEZE.json').read_text(encoding='utf-8'))
        for path, expected in freeze['files'].items():
            if digest(HERE/path) != expected:
                raise RuntimeError('frozen evaluator modified: '+path)
        for path, expected in freeze['retained'].items():
            if digest(repo/path) != expected:
                raise RuntimeError('retained source modified: '+path)
        for name in ('doe_compiler.h', 'doe_compiler_v2.h', 'doe_compiler_job.h'):
            if (owner/'include'/name).read_bytes() != (HERE/'frozen_headers'/name).read_bytes():
                raise RuntimeError('public header changed: '+name)
        sources = sorted(p for p in (owner/'src').glob('*.c') if not p.name.startswith('cli'))
        if not sources:
            raise RuntimeError('no candidate library C sources')
        bound = sources + sorted((owner/'src').glob('*.h')) + sorted((owner/'include').glob('*.h')) + [owner/'src/exports.map']
        save(stage.output/'source-identities.json', {str(p): digest(p) for p in bound})
        if args.prebuilt:
            report['provided_prebuilt'] = dict(path=str(args.prebuilt.resolve()), sha256=digest(args.prebuilt))
            stage.run('needed_provided_prebuilt', ['readelf', '-d', args.prebuilt.resolve()])
            stage.run('exports_provided_prebuilt', ['nm', '-D', '--defined-only', args.prebuilt.resolve()])
            stage.run('imports_provided_prebuilt', ['nm', '-D', '--undefined-only', args.prebuilt.resolve()])
        report['gcc'] = stage.run('gcc_version', ['gcc', '--version']).splitlines()[0]
        report['g++'] = stage.run('gxx_version', ['g++', '--version']).splitlines()[0]
        include = '-I'+str(owner/'include')
        objects = []
        for source in sources:
            obj = stage.build/(source.stem+'.o')
            stage.run('object_'+source.stem, ['gcc', *CFLAGS, '-fPIC', '-fstack-usage', '-fcallgraph-info=su', include, '-c', source, '-o', obj])
            objects.append(obj)
        frames = []
        for path in sorted(stage.build.glob('*.su')):
            for line in path.read_text(encoding='utf-8').splitlines():
                location, size, kind = line.split('\t')
                function = location.split(':')[-1]
                frames.append(dict(location=location, function=function, size=int(size), qualifier=kind, limit=77824 if function=='doe_compiler_compile' else 8192))
        save(stage.output/'frames.json', frames)
        report['frame_failures'] = [r for r in frames if r['size']>r['limit'] or 'unbounded' in r['qualifier']]
        report['frame_max_abi1'] = max(r['size'] for r in frames if r['function']=='doe_compiler_compile')
        report['frame_max_other'] = max(r['size'] for r in frames if r['function']!='doe_compiler_compile')
        archive = stage.build/'libm8-source.a'
        stage.run('archive_source', ['ar', 'rcs', archive, *objects])
        shared = stage.build/'libm8-source.so'
        stage.run('shared_source', ['gcc', '-shared', *objects, '-Wl,--version-script='+str(owner/'src/exports.map'), '-lm', '-o', shared])
        report['source_built'] = {str(p): digest(p) for p in (archive, shared)}
        stage.run('needed_source', ['readelf', '-d', shared])
        symbols = stage.run('exports_source', ['nm', '-D', '--defined-only', shared])
        actual = {line.split()[-1].split('@')[0] for line in symbols.splitlines() if len(line.split())>=3 and line.split()[-2].upper() in {'T', 'W'}}
        report['export_match'] = actual == EXPORTS
        imports = stage.run('imports_source', ['nm', '-u', archive])
        report['allocator_imports'] = sorted(set(ALLOCATORS)&{line.split()[-1].split('@')[0] for line in imports.splitlines() if line.split()})
        stage.run('private_layout_build', ['gcc', *CFLAGS, include, '-I'+str(owner/'src'), HERE/'private_layout_m8.c', '-o', stage.build/'private-layout'], required=False)
        if (stage.build/'private-layout').exists():
            stage.run('private_layout_run', [stage.build/'private-layout'], required=False)
        retained = repo/'runtime/c/compiler/independent_tests_m7'
        wrappers = ['-Wl,'+','.join('--wrap='+name for name in ALLOCATORS)]
        modes = [('source_archive', [archive]), ('source_shared', [shared, '-Wl,-rpath,'+str(stage.build)])]
        if args.prebuilt:
            modes.append(('provided_prebuilt', [args.prebuilt.resolve(), '-Wl,-rpath,'+str(args.prebuilt.resolve().parent)]))
        consumers = [('host', 'gcc', CFLAGS, HERE/'host_environment_m8.c'), ('cpp_m8', 'g++', CPPFLAGS, HERE/'native_m8.cpp'), ('retained_job_c', 'gcc', CFLAGS, retained/'native_job_m7.c'), ('retained_job_cpp', 'g++', CPPFLAGS, retained/'native_job_m7.cpp'), ('retained_layout_cpp', 'g++', CPPFLAGS, retained/'native_layout_m5.cpp')]
        for mode, link in modes:
            for name, compiler, flags, source in consumers:
                binary = stage.build/(mode+'-'+name)
                extra = []
                if mode=='source_archive' and name in {'host', 'retained_job_c'}:
                    extra = ['-DWRAP_ALLOCATORS' if name=='host' else '-DJOB_WRAP_ALLOCATORS', *wrappers]
                built = stage.run('build_'+mode+'_'+name, [compiler, *flags, include, source, *link, *extra, '-pthread', '-lm', '-o', binary], required=False)
                if built is not None:
                    stage.run('run_'+mode+'_'+name, [binary], required=False)
        faults = stage.build/'fenv-faults'
        stage.run('build_fenv_faults', ['gcc', *CFLAGS, include, HERE/'fenv_faults_m8.c', archive, '-Wl,'+','.join('--wrap='+name for name in FENV), '-lm', '-o', faults])
        stage.run('run_fenv_faults', [faults], required=False)
        retained_native = stage.build/'retained-native'
        stage.run('build_retained_native', ['gcc', *CFLAGS, include, retained/'native_m6.c', archive, '-DWRAP_ALLOCATORS', *wrappers, '-pthread', '-lm', '-o', retained_native])
        stage.run('run_retained_native', [retained_native], required=False)
        code = 'import json; from pathlib import Path; from api_job_m7 import JobApi,query_checks,preflight_checks,lifecycle_checks; p=Path('+repr(str(shared))+'); a=JobApi(p); print(json.dumps({"query":query_checks(a),"preflight":preflight_checks(a),"lifecycle":lifecycle_checks(p)},indent=2)); a.close()'
        stage.run('retained_query_preflight_lifecycle', ['python3', '-c', code], required=False, extra_env={'PYTHONPATH':str(retained)})
        code = 'import json; from pathlib import Path; from native_job_m7 import legacy_boundaries; print(json.dumps(legacy_boundaries(Path('+repr(str(shared))+'),Path('+repr(str(stage.output/'legacy'))+')),indent=2))'
        stage.run('retained_legacy_boundaries', ['python3', '-c', code], required=False, extra_env={'PYTHONPATH':str(retained)})
        if not args.skip_sanitizer:
            flags = ['-O1', '-g', '-fno-omit-frame-pointer', '-fsanitize=address,undefined']
            sanitized = stage.build/'libm8-sanitized.so'
            stage.run('build_sanitized_core', ['gcc', *CFLAGS, *flags, '-fPIC', '-shared', include, *sources, '-Wl,--version-script='+str(owner/'src/exports.map'), '-lm', '-o', sanitized])
            for name, source in [('host', HERE/'host_environment_m8.c'), ('retained_job', retained/'native_job_m7.c'), ('retained_native', retained/'native_m6.c')]:
                binary = stage.build/('sanitized-'+name)
                stage.run('build_sanitized_'+name, ['gcc', *CFLAGS, *flags, '-fno-pie', '-no-pie', include, source, sanitized, '-Wl,-rpath,'+str(stage.build), '-pthread', '-lm', '-o', binary])
                stage.run('run_sanitized_'+name, [binary], required=False)
        else:
            report['sanitizer'] = 'NOT RUN, explicitly skipped'
        unchanged = {str(p): digest(p) for p in bound}
        report['source_unchanged_during_run'] = unchanged == json.loads((stage.output/'source-identities.json').read_text(encoding='utf-8'))
        report['failures'] = stage.failures
        if not stage.failures and not report['frame_failures'] and report['export_match'] and not report['allocator_imports'] and report['source_unchanged_during_run']:
            report['status'] = 'PASS'
    except (OSError, ValueError, RuntimeError) as error:
        report['error'] = str(error)
    save(stage.output/'native-report.json', report)
    print(json.dumps(report, indent=2), flush=True)
    return int(report['status']!='PASS')


if __name__=='__main__':
    raise SystemExit(main())
