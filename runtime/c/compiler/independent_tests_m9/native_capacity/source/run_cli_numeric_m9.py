"""New numeric input through unchanged M7 CLI ownership/fault checker.

Run with PYTHONPATH pointing at accepted independent_tests_m7. Historical files
and their expected rollback behavior are unchanged. Only the explicitly frozen
source input is supplied in this isolated process instead of M5 STORAGE.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess

import cli_m5
from cli_job_m7 import Checks, WRAPS
from run_native_m9 import CFLAGS, Stage, digest, save

HERE = Path(__file__).resolve().parent
FENV = ['fegetenv', 'feholdexcept', 'fesetenv', 'feupdateenv', 'fesetround']


def extra_checks(checks: Checks, binary: Path, label: str) -> None:
    reference = None
    for mode in cli_m5.MODES:
        directory = checks.directory(label+'_numeric_'+mode)
        (directory/'source.wgsl').write_bytes(cli_m5.STORAGE)
        run, trace = checks.invoke(binary, checks.parts(directory, mode), directory)
        errors = []
        if run.returncode or trace is None:
            checks.record(label+'_numeric_'+mode, ['numeric source not accepted'], stderr=run.stderr, returncode=run.returncode)
            continue
        if trace['allocations'] != {'binary':4, 'legacy':3, 'v2':5}[mode]:
            errors.append('M8 added CLI allocations or changed frozen allocation count')
        module = (directory/'module.spv').read_bytes()
        if reference is not None and module != reference:
            errors.append('CLI modes emit different current words')
        reference = module
        errors += checks.validate(directory/'module.spv')
        if mode=='binary':
            if (directory/'reflection.json').exists() or 32768 in trace['sizes']:
                errors.append('binary mode generated or allocated optional sidecar')
        else:
            reflection = json.loads((directory/'reflection.json').read_text(encoding='utf-8'))
            if reflection['entry_point']!='main' or reflection['binding_count']!=1 or reflection['workgroup_size']!=[1,1,1]:
                errors.append('numeric source reflection mismatch')
        checks.record(label+'_numeric_'+mode, errors, trace=trace, stderr=run.stderr, returncode=run.returncode)
        calls = Counter()
        for line in run.stderr.splitlines():
            if line.startswith('M8_ENV_CALL '):
                _, name, ordinal = line.split()
                calls[name] = max(calls[name], int(ordinal))
        for name, count in sorted(calls.items()):
            for ordinal in range(1, count+1):
                target = checks.directory(label+'_env_'+mode+'_'+name+'_'+str(ordinal))
                (target/'source.wgsl').write_bytes(cli_m5.STORAGE)
                before = cli_m5.tree(target)
                command = [str(binary), *map(str, checks.parts(target, mode))]
                env = dict(os.environ, M8_ENV_FAULT_OPERATION=name, M8_ENV_FAULT_CALL=str(ordinal), ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
                failed = subprocess.run(command, capture_output=True, text=True, timeout=60, check=False, env=env)
                checks.commands.append(dict(command=command, returncode=failed.returncode, stdout=failed.stdout, stderr=failed.stderr))
                faults = [line for line in failed.stderr.splitlines() if line.startswith('M8_ENV_FAULT ')]
                ownership = [json.loads(line[len('M7_TRACE '):]) for line in failed.stderr.splitlines() if line.startswith('M7_TRACE ')]
                errors = []
                if failed.returncode==0 or len(faults)!=1:
                    errors.append('host-state failure succeeded or exact fault not reached')
                if cli_m5.tree(target)!=before:
                    errors.append('host-state failure published output/sidecar or left residue')
                if len(ownership)!=1 or ownership[0]['live'] or ownership[0]['live_descriptors']:
                    errors.append('host-state failure leaked owned storage/descriptors')
                checks.record(label+'_env_'+mode+'_'+name+'_'+str(ordinal), errors, stderr=failed.stderr, returncode=failed.returncode)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', required=True, type=Path)
    parser.add_argument('--retained-root', required=True, type=Path)
    parser.add_argument('--validator', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--skip-sanitizer', action='store_true')
    args = parser.parse_args()
    output=args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    owner=args.source_root.resolve()
    repo=args.retained_root.resolve()
    retained=repo/'runtime/c/compiler/independent_tests_m7'
    freeze=json.loads((HERE/'CLI_PRE_CANDIDATE_FREEZE.json').read_text(encoding='utf-8'))
    for path, expected in freeze['retained'].items():
        if digest(repo/path)!=expected:
            raise RuntimeError('historical CLI helper modified: '+path)
    for path, expected in freeze['files'].items():
        if digest(HERE/path)!=expected:
            raise RuntimeError('numeric CLI fixture modified: '+path)
    cli_m5.STORAGE=(HERE/'numeric_fixture_m9.wgsl').read_bytes()
    stage=Stage(output/'build_receipts')
    source_files=sorted((owner/'src').glob('*.c'))
    save(output/'source-identities.json',{str(p):digest(p) for p in [*source_files,retained/'job_cli_io_wrap_m7.c',HERE/'cli_fenv_wrap_m9.c']})
    checks=Checks(argparse.Namespace(repo=repo,source_root=owner,cli=stage.build/'ordinary',validator=args.validator.resolve(),evidence=output,baseline=None))
    try:
        for label, extra in [('ordinary',[]), *([] if args.skip_sanitizer else [('sanitized',['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie'])])]:
            binary=stage.build/label
            stage.run('build_'+label,['gcc',*CFLAGS,*extra,'-I'+str(owner/'include'),*source_files,retained/'job_cli_io_wrap_m7.c',HERE/'cli_fenv_wrap_m9.c','-Wl,'+','.join('--wrap='+name for name in [*WRAPS,*FENV]),'-lm','-o',binary])
            extra_checks(checks,binary,label)
            checks.faults(binary,label)
    except (RuntimeError,OSError,ValueError,subprocess.TimeoutExpired) as error:
        checks.record('stage_exception',[str(error)])
    return checks.finish()


if __name__=='__main__':
    raise SystemExit(main())
