"""Portable selected-source M5 acceptance front door; installed toolchain only."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
LIB_SOURCES=('numeric','lex','parse','validate','reflection','spirv','compile')
CFLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror']

def parse_args() -> argparse.Namespace:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',required=True,type=Path,help='Current Doe checkout containing unchanged source corpus and consumer.')
    parser.add_argument('--source-root',type=Path,help='Compiler owner snapshot; defaults to repo/runtime/c/compiler.')
    parser.add_argument('--validator',required=True,type=Path,help='Explicit already-installed spirv-val binary.')
    parser.add_argument('--evidence',required=True,type=Path,help='Fresh evidence directory; no existing output is overwritten.')
    parser.add_argument('--cc',type=Path,default=Path(shutil.which('gcc') or '/nonexistent/gcc'),help='Existing GCC C compiler; no installs.')
    parser.add_argument('--baseline-library',type=Path,help='Optional M4 shared library for additional fresh byte comparison.')
    parser.add_argument('--baseline-cli',type=Path,help='Optional M4 CLI for additional fresh legacy byte comparison.')
    parser.add_argument('--plan',action='store_true',help='Validate paths and print exact commands without building or running.')
    return parser.parse_args()

def main() -> int:
    a=parse_args()
    for key in ['repo','source_root','validator','evidence','cc','baseline_library','baseline_cli']:
        value=getattr(a,key)
        if value is not None:setattr(a,key,value.resolve())
    owner=a.source_root or a.repo/'runtime/c/compiler'
    for path in [a.cc,a.validator,owner/'src/compile.c',a.repo/'bench/gates/spirv_val_gate.py']:
        if not path.is_file():raise FileNotFoundError(path)
    build=a.evidence/'build';cli=build/'doe-compiler-c';library=build/'libdoe_compiler.so'
    lib_sources=[str(owner/'src'/(name+'.c')) for name in LIB_SOURCES]
    commands=[('build_library',[str(a.cc),*CFLAGS,'-fPIC','-shared',*lib_sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-o',str(library)]),('build_cli',[str(a.cc),*CFLAGS,*lib_sources,str(owner/'src/cli.c'),str(owner/'src/cli_json.c'),'-o',str(cli)])]
    def stage(name: str, script: str, extra: list) -> None:
        commands.append((name,[sys.executable,str(HERE/script),*[str(v) for v in extra]]))
    stage('semantic','semantic_m5.py',['--repo',a.repo,'--library',library,'--validator',a.validator,'--evidence',a.evidence/'semantic'])
    stage('retained','blackbox_m4.py',['--root',a.repo,'--library',library,'--validator',a.validator,'--out',a.evidence/'retained'])
    stage('equivalence','retained_equivalence_m5.py',['--repo',a.repo,'--library',library,'--validator',a.validator,'--evidence',a.evidence/'equivalence',*(['--baseline',a.baseline_library] if a.baseline_library else [])])
    stage('retained_mutations','test_oracle_m4.py',['--root',a.repo,'--modules',a.evidence/'retained/modules','--out',a.evidence/'retained-mutations.json'])
    stage('cli','cli_m5.py',['--repo',a.repo,'--cli',cli,'--source-root',owner,'--validator',a.validator,'--evidence',a.evidence/'cli',*(['--baseline',a.baseline_cli] if a.baseline_cli else [])])
    stage('corpus','corpus_m5.py',['--repo',a.repo,'--cli',cli,'--validator',a.validator,'--evidence',a.evidence/'corpus'])
    stage('corpus_execution','corpus_semantics_m5.py',['--repo',a.repo,'--corpus',a.evidence/'corpus/results.json','--evidence',a.evidence/'corpus-execution'])
    stage('corpus_phases','corpus_phases_m5.py',['--repo',a.repo,'--source-root',owner,'--corpus',a.evidence/'corpus/results.json','--execution',a.evidence/'corpus-execution/results.json','--output',a.evidence/'corpus-coverage-m5.json'])
    stage('native','native_stage_m5.py',['--source-root',owner,'--library',library,'--validator',a.validator,'--evidence',a.evidence/'native','--corpus',a.evidence/'semantic/sources'])
    if a.plan:
        print(json.dumps(dict(commands=commands,notes='No builds/tests executed by --plan.'),indent=2));return 0
    a.evidence.mkdir(parents=True,exist_ok=False);build.mkdir()
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1');records=[]
    for name,command in commands:
        start=time.monotonic();process=subprocess.run(command,cwd=HERE,env=env,capture_output=True,text=True)
        (a.evidence/(name+'.log')).write_text(process.stdout+process.stderr,encoding='utf-8')
        records.append(dict(stage=name,command=command,returncode=process.returncode,elapsed_seconds=time.monotonic()-start))
        (a.evidence/'commands.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
        if process.returncode:
            print('FAIL '+name+'; first failure retained in '+str(a.evidence/(name+'.log')));return 1
        print('PASS '+name,flush=True)
    artifacts={str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for p in build.iterdir() if p.is_file()}
    (a.evidence/'summary.json').write_text(json.dumps(dict(status='PASS',stages=len(records),artifacts=artifacts,physical_execution=False,runtime_adoption=False,full_migration=False),indent=2)+'\n',encoding='utf-8')
    return 0
if __name__=='__main__':raise SystemExit(main())
