"""Portable source-only independent M8 evaluator; focused stages never imply full acceptance."""
from __future__ import annotations
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
CFLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow',
        '-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror',
        '-fno-fast-math','-ffp-contract=off','-frounding-math']


def digest(path: Path) -> str:return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','source-root','validator','evidence']:parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--existing-build',type=Path,help='Explicit already source-built DSO/CLI, attribution retained.')
    parser.add_argument('--prebuilt',type=Path,help='Explicit separately supplied native-only prebuilt library.')
    parser.add_argument('--stages',help='Comma-separated stages; omissions are not passes.')
    parser.add_argument('--plan',action='store_true');parser.add_argument('--import-check',action='store_true')
    args=parser.parse_args()
    for k,v in vars(args).items():
        if isinstance(v,Path):setattr(args,k,v.resolve())
    retained=args.repo/'runtime/c/compiler/independent_tests_m7'
    env=dict(os.environ,PYTHONPATH=os.pathsep.join([str(HERE),str(retained)]),PYTHONDONTWRITEBYTECODE='1')
    if args.import_check:
        names=[p.stem for p in HERE.glob('*.py') if p.stem!='run_acceptance_m8']
        proc=subprocess.run([sys.executable,'-c','import importlib; '+ '; '.join('importlib.import_module('+repr(n)+')' for n in names)],env=env,check=False)
        print(json.dumps(dict(candidate_executed=False,ordinary_imports=len(names),passed=proc.returncode==0)));return proc.returncode
    owner=args.source_root;build=args.existing_build or args.evidence/'build';library=build/'libdoe_compiler.so';cli=build/'doe-compiler-c'
    sources=sorted(p for p in (owner/'src').glob('*.c') if not p.name.startswith('cli'))
    commands=[]
    if not args.existing_build:
        commands += [('build_library',['gcc',*CFLAGS,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-lm','-o',library]),
                     ('build_cli',['gcc',*CFLAGS,*sources,owner/'src/cli.c',owner/'src/cli_json.c','-lm','-o',cli])]
    def stage(name: str,script: Path,arguments: list) -> None:commands.append((name,[sys.executable,script,*arguments]))
    stage('exact_oracle',HERE/'exact_numeric_m8.py',[])
    stage('oracle',HERE/'test_oracle_m8.py',['--validator',args.validator,'--out',args.evidence/'oracle'])
    for name in ['scalar','constants','witnesses']:
        stage(name,HERE/'semantic_m8.py',['--repo',args.repo,'--library',library,'--validator',args.validator,'--out',args.evidence/name,'--stage',name,'--shared-apis','--apply-errata'])
    stage('capacity',HERE/'capacity_m8.py',['--library',library,'--validator',args.validator,'--out',args.evidence/'capacity','--apply-errata'])
    stage('retained',HERE/'retained_m8.py',['--repo',args.repo,'--library',library,'--validator',args.validator,'--out',args.evidence/'retained'])
    stage('postdisclosure',HERE/'supplemental_m8.py',['--library',library,'--validator',args.validator,'--out',args.evidence/'postdisclosure'])
    stage('mutations',HERE/'mutations_m8.py',['--repo',args.repo,'--scalar-modules',args.evidence/'scalar/modules','--constant-modules',args.evidence/'constants/modules','--validator',args.validator,'--out',args.evidence/'mutations'])
    stage('corpus',HERE/'corpus_m8.py',['--repo',args.repo,'--source-root',owner,'--cli',cli,'--validator',args.validator,'--out',args.evidence/'corpus'])
    stage('native',HERE/'native_host/run_native_m8.py',['--source-root',owner,'--retained-root',args.repo,'--out',args.evidence/'native',*(['--prebuilt',args.prebuilt] if args.prebuilt else [])])
    stage('cli',HERE/'native_host/run_cli_numeric_m8.py',['--source-root',owner,'--retained-root',args.repo,'--validator',args.validator,'--out',args.evidence/'cli'])
    if args.stages:
        selected=set(args.stages.split(','));known={n for n,c in commands}
        if selected-known:raise ValueError('Unknown stages: '+str(selected-known))
        commands=[(n,c) for n,c in commands if n in selected]
    commands=[(n,list(map(str,c))) for n,c in commands]
    if args.plan:print(json.dumps(dict(commands=commands,candidate_executed=False,private_archive_required=False),indent=2));return 0
    args.evidence.mkdir(parents=True,exist_ok=False)
    if not args.existing_build:build.mkdir()
    bound=[*sorted((owner/'src').glob('*.*')),*sorted((owner/'include').glob('*.h'))]
    identity={str(p.relative_to(owner)):digest(p) for p in bound if p.is_file()}
    (args.evidence/'source-identity.json').write_text(json.dumps(identity,indent=2,sort_keys=True)+'\n')
    records=[]
    for name,command in commands:
        start=time.monotonic();run=subprocess.run(command,capture_output=True,text=True,check=False,env=env,cwd=args.repo)
        (args.evidence/(name+'.log')).write_text(run.stdout+run.stderr)
        records.append(dict(stage=name,command=command,returncode=run.returncode,elapsed_seconds=time.monotonic()-start))
        (args.evidence/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
        print(('FAIL ' if run.returncode else 'PASS ')+name,flush=True)
        if run.returncode:return 1
    if identity!={str(p.relative_to(owner)):digest(p) for p in bound if p.is_file()}:raise RuntimeError('Candidate source changed during execution')
    summary=dict(passed=True,full_stage_selection=not args.stages,stages=len(records),existing_build=str(args.existing_build) if args.existing_build else None,
                 artifacts={p.name:digest(p) for p in [library,cli] if p.exists()},physical_execution=False,runtime_adoption=False)
    (args.evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');return 0


if __name__=='__main__':raise SystemExit(main())
