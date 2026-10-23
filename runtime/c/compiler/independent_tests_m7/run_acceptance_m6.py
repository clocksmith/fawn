"""Portable selected-source M6 acceptance; installed tools, explicit current inputs."""
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
LIB_SOURCES=('numeric','lex','parse','validate','reflection','spirv','compile')
CFLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow',
        '-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror']


def parse_args() -> argparse.Namespace:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','validator','evidence']:
        p.add_argument('--'+key,type=Path,required=True,help='Explicit '+key+' path.')
    p.add_argument('--source-root',type=Path,help='Frozen compiler owner; defaults to current repo owner.')
    p.add_argument('--existing-build',type=Path,help='Explicit existing GCC library/CLI build; skip only central builds.')
    p.add_argument('--cc',type=Path,default=Path(shutil.which('gcc') or '/nonexistent/gcc'),help='Installed GCC only.')
    p.add_argument('--cxx',type=Path,default=Path(shutil.which('g++') or '/nonexistent/g++'),help='Installed G++ only.')
    p.add_argument('--baseline-library',type=Path,help='Optional report-only historical library.')
    p.add_argument('--baseline-cli',type=Path,help='Optional report-only historical CLI.')
    p.add_argument('--plan',action='store_true',help='Validate inputs and print stage commands; no execution.')
    p.add_argument('--import-check',action='store_true',help='Import selected Python sources only; no candidate execution.')
    p.add_argument('--smoke',action='store_true',help='Run only four decisive M6 semantic positives, not full campaigns.')
    return p.parse_args()


def main() -> int:
    a=parse_args()
    for k,v in vars(a).items():
        if isinstance(v,Path):setattr(a,k,v.resolve())
    owner=a.source_root or a.repo/'runtime/c/compiler'
    build=a.existing_build or a.evidence/'build'
    library=build/'libdoe_compiler.so';cli=build/'doe-compiler-c'
    for path in [a.cc,a.cxx,a.validator,owner/'src/compile.c',a.repo/'bench/gates/spirv_val_gate.py']:
        if not path.is_file():raise FileNotFoundError(path)
    if a.import_check:
        modules=[p.stem for p in HERE.glob('*.py') if p.name!='run_acceptance_m6.py']
        for module in sorted(modules):importlib.import_module(module)
        print(json.dumps(dict(status='PASS',ordinary_imports=len(modules),candidate_executed=False)))
        return 0
    sources=[str(owner/'src'/(name+'.c')) for name in LIB_SOURCES]
    commands=[]
    if not a.existing_build:
        commands.extend([
            ('build_library',[a.cc,*CFLAGS,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-o',library]),
            ('build_cli',[a.cc,*CFLAGS,*sources,owner/'src/cli.c',owner/'src/cli_json.c','-o',cli])])
    def stage(name: str, script: str, arguments: list) -> None:
        commands.append((name,[sys.executable,HERE/script,*arguments]))
    stage('semantic','semantic_m6.py',['--repo',a.repo,'--library',library,'--validator',a.validator,'--out',a.evidence/'semantic',*(['--smoke'] if a.smoke else [])])
    if not a.smoke:
        stage('retained','retained_m6.py',['--repo',a.repo,'--build',build,'--validator',a.validator,'--out',a.evidence/'retained',*(['--baseline',a.baseline_library] if a.baseline_library else [])])
        stage('corpus','corpus_m6.py',['--repo',a.repo,'--source-root',owner,'--build',build,'--validator',a.validator,'--out',a.evidence/'corpus',*(['--baseline',a.baseline_cli] if a.baseline_cli else [])])
        stage('native_cli','native_m6.py',['--repo',a.repo,'--source-root',owner,'--build',build,'--library',library,'--cli',cli,'--validator',a.validator,'--cc',a.cc,'--cxx',a.cxx,'--corpus',a.evidence/'semantic/sources','--out',a.evidence/'native',*(['--baseline-cli',a.baseline_cli] if a.baseline_cli else [])])
    commands=[(name,[str(arg) for arg in command]) for name,command in commands]
    if a.plan:
        print(json.dumps(dict(commands=commands,candidate_executed=False,baseline_required=False),indent=2));return 0
    a.evidence.mkdir(parents=True,exist_ok=False)
    if not a.existing_build:build.mkdir()
    records=[]
    for name,command in commands:
        start=time.monotonic()
        run=subprocess.run(command,cwd=HERE,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'),capture_output=True,text=True,check=False)
        (a.evidence/(name+'.log')).write_text(run.stdout+run.stderr,encoding='utf-8')
        records.append(dict(stage=name,command=command,returncode=run.returncode,elapsed_seconds=time.monotonic()-start))
        (a.evidence/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if run.returncode:
            print('FAIL '+name+'; first failure retained in '+str(a.evidence/(name+'.log')));return 1
        print('PASS '+name,flush=True)
    artifacts={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [library,cli] if p.is_file()}
    result=dict(status='PASS',smoke_only=a.smoke,stages=len(records),artifacts=artifacts,
                physical_execution=False,runtime_adoption=False,full_migration=False)
    (a.evidence/'summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return 0

if __name__=='__main__':raise SystemExit(main())
