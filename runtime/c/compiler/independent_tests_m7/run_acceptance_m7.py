"""Portable M7 independent source-only acceptance front door.

No archived artifact is required. Each run uses explicit candidate source and
installed tools; imports never modify sys.path. Partial stage selections report
only their selected evidence, never full acceptance.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
CFLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow',
        '-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror']


def parse_args() -> argparse.Namespace:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','validator','evidence']:
        parser.add_argument('--'+key,type=Path,required=True,help='Explicit '+key+' path.')
    parser.add_argument('--source-root',type=Path,help='Stable compiler source owner; default current repo owner.')
    parser.add_argument('--existing-build',type=Path,help='Explicit GCC shared library and CLI; omit only builds.')
    parser.add_argument('--cc',type=Path,default=Path(shutil.which('gcc') or '/nonexistent/gcc'),help='Installed native C compiler.')
    parser.add_argument('--cxx',type=Path,default=Path(shutil.which('g++') or '/nonexistent/g++'),help='Installed native C++ compiler.')
    parser.add_argument('--stages',help='Comma-separated focused stages, default all. Never implies full acceptance.')
    parser.add_argument('--plan',action='store_true',help='Print commands without executing candidate.')
    parser.add_argument('--import-check',action='store_true',help='Import source modules only; no candidate access.')
    return parser.parse_args()


def main() -> int:
    args=parse_args()
    for key,value in vars(args).items():
        if isinstance(value,Path):setattr(args,key,value.resolve())
    if args.import_check:
        modules=sorted(path.stem for path in HERE.glob('*.py') if path.stem!='run_acceptance_m7')
        for name in modules:importlib.import_module(name)
        print(json.dumps(dict(status='PASS',ordinary_imports=len(modules),candidate_executed=False)))
        return 0
    owner=args.source_root or args.repo/'runtime/c/compiler'
    build=args.existing_build or args.evidence/'build'
    library=build/'libdoe_compiler.so';cli=build/'doe-compiler-c'
    for path in [args.cc,args.cxx,args.validator,owner/'src/compile.c',args.repo/'bench/gates/spirv_val_gate.py']:
        if not path.is_file():raise FileNotFoundError(path)
    sources=sorted(path for path in (owner/'src').glob('*.c') if not path.name.startswith('cli'))
    commands=[('oracle_selftest',[sys.executable,'-m','unittest','-v','test_oracle_m7'])]
    if not args.existing_build:
        commands += [('build_library',[args.cc,*CFLAGS,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-o',library]),
                     ('build_cli',[args.cc,*CFLAGS,*sources,owner/'src/cli.c',owner/'src/cli_json.c','-o',cli])]
    def stage(name: str,script: str,arguments: list) -> None:
        commands.append((name,[sys.executable,HERE/script,*arguments]))
    stage('semantic_m7','semantic_m7.py',['--library',library,'--validator',args.validator,'--out',args.evidence/'semantic_m7'])
    stage('semantic_m6','semantic_m6.py',['--repo',args.repo,'--library',library,'--validator',args.validator,'--out',args.evidence/'semantic_m6'])
    stage('retained_m1_m5','retained_m7.py',['--repo',args.repo,'--build',build,'--validator',args.validator,'--out',args.evidence/'retained_m1_m5'])
    stage('job_api','api_job_m7.py',['--library',library,'--out',args.evidence/'job_api'])
    stage('empty_module_amendment','job_empty_module_all_modes_m7.py',['--library',library,'--cli',cli,'--out',args.evidence/'empty_module_amendment'])
    stage('native_job','native_job_m7.py',['--repo',args.repo,'--source-root',owner,'--build',build,'--library',library,'--cc',args.cc,'--cxx',args.cxx,'--out',args.evidence/'native_job'])
    stage('cli_job','cli_job_m7.py',['--repo',args.repo,'--source-root',owner,'--build',build,'--library',library,'--cli',cli,'--cc',args.cc,'--cxx',args.cxx,'--validator',args.validator,'--out',args.evidence/'cli_job'])
    stage('witnesses','witnesses_m7.py',['--repo',args.repo,'--library',library,'--cli',cli,'--validator',args.validator,'--out',args.evidence/'witnesses'])
    stage('corpus','corpus_m7.py',['--repo',args.repo,'--build',build,'--source-root',owner,'--validator',args.validator,'--out',args.evidence/'corpus'])
    all_names={name for name,_ in commands}
    if args.stages:
        selected=set(args.stages.split(','))
        if selected-all_names:raise ValueError('Unknown stages: '+str(selected-all_names))
        commands=[row for row in commands if row[0] in selected]
    commands=[(name,[str(x) for x in command]) for name,command in commands]
    if args.plan:
        print(json.dumps(dict(commands=commands,candidate_executed=False,archived_baseline_required=False),indent=2));return 0
    args.evidence.mkdir(parents=True,exist_ok=False)
    if not args.existing_build:build.mkdir()
    identity={str(path.relative_to(owner)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(owner.rglob('*')) if path.is_file() and ('src' in path.relative_to(owner).parts or 'include' in path.relative_to(owner).parts)}
    (args.evidence/'source_identity.json').write_text(json.dumps(identity,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    records=[]
    for name,command in commands:
        start=time.monotonic()
        run=subprocess.run(command,cwd=HERE,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'),capture_output=True,text=True,check=False)
        (args.evidence/(name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        records.append(dict(stage=name,command=command,returncode=run.returncode,elapsed_seconds=time.monotonic()-start))
        (args.evidence/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if run.returncode:
            print('FAIL '+name+'; exact command and first failure retained.');return 1
        print('PASS '+name,flush=True)
    after={str(path.relative_to(owner)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(owner.rglob('*')) if path.is_file() and ('src' in path.relative_to(owner).parts or 'include' in path.relative_to(owner).parts)}
    if identity!=after:raise RuntimeError('Candidate source changed during campaign')
    artifacts={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in [library,cli]}
    summary=dict(status='PASS',full_stage_selection=not bool(args.stages),stages=len(records),artifacts=artifacts,source_unchanged=True,physical_execution=False,runtime_adoption=False,full_migration=False)
    (args.evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    return 0

if __name__=='__main__':raise SystemExit(main())
