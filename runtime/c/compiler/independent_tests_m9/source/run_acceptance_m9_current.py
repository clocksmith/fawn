"""Current source-only M9 gate, with explicit narrow evaluator adapters."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent
CFLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror','-fno-fast-math','-ffp-contract=off','-frounding-math']


def digest(path: Path) -> str:return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ['repo','source-root','validator','evidence']:parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--existing-build',type=Path);parser.add_argument('--prebuilt',type=Path)
    parser.add_argument('--native-source',type=Path,default=HERE.parent/'native_capacity/source')
    parser.add_argument('--stages');parser.add_argument('--plan',action='store_true')
    parser.add_argument('--native-compact',action='store_true',help='Use guarded low-disk native orchestration; same checks')
    args=parser.parse_args()
    for k,v in vars(args).items():
        if isinstance(v,Path):setattr(args,k,v.resolve())
    old=args.repo/'runtime/c/compiler/independent_tests_m8';older=args.repo/'runtime/c/compiler/independent_tests_m7'
    env=dict(os.environ,PYTHONPATH=os.pathsep.join(map(str,[HERE,old,older])),PYTHONDONTWRITEBYTECODE='1')
    owner=args.source_root;build=args.existing_build or args.evidence/'build';library=build/'libdoe_compiler.so';cli=build/'doe-compiler-c'
    sources=sorted(p for p in (owner/'src').glob('*.c') if not p.name.startswith('cli'))
    commands=[]
    if not args.existing_build:
        commands += [('build_library',['gcc',*CFLAGS,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-lm','-o',library]),('build_cli',['gcc',*CFLAGS,*sources,owner/'src/cli.c',owner/'src/cli_json.c','-lm','-o',cli])]
    def stage(name: str,script: Path,options: list) -> None:commands.append((name,[sys.executable,script,*options]))
    stage('interval_oracle',HERE/'interval_math_m9.py',[])
    stage('unfinished_adapter_controls',HERE/'retained_m9_corrected.py',['--controls'])
    stage('instruction_oracle',HERE/'test_oracle_m9.py',['--validator',args.validator,'--out',args.evidence/'instruction_oracle'])
    # One common module directory allows predeclared mutations across semantic families.
    stage('semantics',HERE/'semantic_m9.py',['--repo',args.repo,'--library',library,'--validator',args.validator,'--out',args.evidence/'semantics','--shared-apis'])
    stage('mutations',HERE/'mutations_m9.py',['--repo',args.repo,'--modules',args.evidence/'semantics/modules','--validator',args.validator,'--out',args.evidence/'mutations'])
    for label in ['historical','m8']:
        stage('retained_'+label,HERE/'retained_m9_corrected.py',['--repo',args.repo,'--library',library,'--validator',args.validator,'--out',args.evidence/('retained_'+label),'--stage',label])
    stage('retained_m8_mutations',old/'mutations_m8.py',['--repo',args.repo,'--scalar-modules',args.evidence/'retained_m8/modules','--constant-modules',args.evidence/'retained_m8/modules','--validator',args.validator,'--out',args.evidence/'retained_m8_mutations'])
    stage('retained_capacity',old/'capacity_m8.py',['--library',library,'--validator',args.validator,'--out',args.evidence/'retained_capacity','--apply-errata'])
    stage('retained_postdisclosure',old/'supplemental_m8.py',['--library',library,'--validator',args.validator,'--out',args.evidence/'retained_postdisclosure'])
    stage('corpus',HERE/'corpus_m9.py',['--repo',args.repo,'--source-root',owner,'--cli',cli,'--validator',args.validator,'--out',args.evidence/'corpus'])
    stage('native',args.native_source/'run_all_m9_current.py',['--source-root',owner,'--retained-root',args.repo,'--validator',args.validator,'--out',args.evidence/'native',*(['--prebuilt',args.prebuilt] if args.prebuilt else []),*(['--compact'] if args.native_compact else [])])
    if args.stages:
        selected=set(args.stages.split(','));known={n for n,c in commands}
        if selected-known:raise ValueError('unknown stage '+repr(selected-known))
        commands=[(n,c) for n,c in commands if n in selected]
    commands=[(n,list(map(str,c))) for n,c in commands]
    if args.plan:print(json.dumps(dict(commands=commands,candidate_executed=False,source_only=True),indent=2));return 0
    seal=json.loads((HERE/'CURRENT_SOURCE_MANIFEST.json').read_text())
    for name,expected in seal['files'].items():
        if digest(HERE/name)!=expected:raise RuntimeError('evaluator source changed: '+name)
    for name,expected in json.loads((HERE/'IMPORTED_SOURCE.json').read_text()).items():
        if digest(args.repo/name)!=expected:raise RuntimeError('retained evaluator source changed: '+name)
    args.evidence.mkdir(parents=True,exist_ok=False)
    if not args.existing_build:build.mkdir()
    bound=[*sorted((owner/'src').glob('*.*')),*sorted((owner/'include').glob('*.h'))]
    identity={str(p.relative_to(owner)):digest(p) for p in bound if p.is_file()}
    (args.evidence/'source_identity.json').write_text(json.dumps(identity,indent=2,sort_keys=True)+'\n')
    records=[]
    for name,command in commands:
        start=time.monotonic();proc=subprocess.run(command,capture_output=True,text=True,env=env,cwd=args.repo,check=False)
        (args.evidence/(name+'.log')).write_text(proc.stdout+proc.stderr)
        records.append(dict(stage=name,returncode=proc.returncode,command=command,seconds=time.monotonic()-start))
        (args.evidence/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
        print(('FAIL ' if proc.returncode else 'PASS ')+name,flush=True)
        if proc.returncode:return 1
    if identity!={str(p.relative_to(owner)):digest(p) for p in bound if p.is_file()}:raise RuntimeError('candidate source changed during execution')
    summary=dict(passed=True,full_stage_selection=not args.stages,stages=len(records),physical_execution=False,runtime_adoption=False)
    (args.evidence/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');return 0


if __name__=='__main__':raise SystemExit(main())
