"""Reproduce source-only M4 acceptance with installed GCC/G++ and explicit validator."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE=Path(__file__).resolve().parent
FROZEN={'MILESTONE_1.md':'43068d669ee01f4fe269e458a335adf7e6121b09a44fe13312aceff4a58fc6b5',
    'MILESTONE_2.md':'d32a02844cd9c1e618cb15a8d25f97ed959a13d66e126fe4d80605e5b432fdac',
    'MILESTONE_4.md':'e87de34c7167ee97fca5071a3fd58bf8492f16a63e60150aca72f4519b8e182b',
    'include/doe_compiler.h':'5bf4f5e5a4e4624c094320ad9ec97119278bc7f6f06b0e47c01e444d42c6dc5b'}


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo-root',type=Path,required=True,help='Doe source root; no source file is edited.')
    parser.add_argument('--compiler-root',type=Path,required=True,help='Current native C compiler root including numeric.c.')
    parser.add_argument('--validator',type=Path,required=True,help='Already installed approved spirv-val executable.')
    parser.add_argument('--out',type=Path,required=True,help='Fresh output directory, must not already exist.')
    parser.add_argument('--dry-run',action='store_true',help='Validate inputs and command graph without building/running stages or creating output.')
    args=parser.parse_args()
    for key in ['repo_root','compiler_root','validator','out']:setattr(args,key,getattr(args,key).resolve())
    for name,want in FROZEN.items():
        if hashlib.sha256((args.compiler_root/name).read_bytes()).hexdigest()!=want:raise SystemExit('Frozen contract/header changed: '+name)
    snapshot=args.out/'compiler';native=args.out/'native';library=native/'libdoe_compiler.so';cli=native/'doe-compiler-c'
    names=['lex','numeric','parse','validate','spirv','compile'];sources=[snapshot/'src'/(name+'.c') for name in names]
    for name in [*names,'cli']:
        if not (args.compiler_root/'src'/(name+'.c')).is_file():raise SystemExit('Missing source owner '+name)
    if not args.validator.is_file():raise SystemExit('Missing approved validator')
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1');commands=[]
    def stage(name,command):
        command=list(map(str,command))
        if args.dry_run:
            commands.append(dict(name=name,command=command));return
        result=subprocess.run(command,capture_output=True,text=True,env=env,cwd=HERE)
        (args.out/(name+'.log')).write_text(result.stdout+result.stderr)
        commands.append(dict(name=name,command=command,returncode=result.returncode))
        (args.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
        if result.returncode:raise SystemExit(result.returncode)
    if not args.dry_run:
        args.out.mkdir(parents=True,exist_ok=False);native.mkdir();snapshot.mkdir()
        for directory in ['src','include']:shutil.copytree(args.compiler_root/directory,snapshot/directory)
        for name in FROZEN:
            if '/' not in name:shutil.copy2(args.compiler_root/name,snapshot/name)
        bound={str(path.relative_to(args.compiler_root)):hashlib.sha256(path.read_bytes()).hexdigest() for directory in ['src','include'] for path in (args.compiler_root/directory).glob('*') if path.is_file()}
        (args.out/'input-source-hashes.json').write_text(json.dumps(bound,indent=2)+'\n')
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror']
    stage('source_library',['gcc',*flags,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(snapshot/'src/exports.map'),'-o',library])
    stage('source_cli',['gcc',*flags,*sources,snapshot/'src/cli.c','-o',cli])
    common=['--root',args.repo_root,'--library',library,'--validator',args.validator]
    stage('blackbox',[sys.executable,HERE/'blackbox_m4.py',*common,'--out',args.out/'blackbox'])
    stage('native_cli_sanitizers',[sys.executable,HERE/'native_stage_m4.py',*common,'--compiler-root',snapshot,'--cli',cli,'--out',args.out/'native-checks'])
    stage('oracle_m1',[sys.executable,HERE/'test_oracle.py','--module',args.out/'blackbox/modules/real_residual.spv','--out',args.out/'oracle-m1.json'])
    for milestone in ['m2','m4']:
        stage('oracle_'+milestone,[sys.executable,HERE/('test_oracle_'+milestone+'.py'),'--root',args.repo_root,'--modules',args.out/'blackbox/modules','--out',args.out/('oracle-'+milestone+'.json')])
    stage('supplemental',[sys.executable,HERE/'supplemental_m4.py','--library',library,'--validator',args.validator,'--out',args.out/'supplemental'])
    stage('corpus_audit',[sys.executable,HERE/'audit_sources_m4.py',*common,'--compiler-root',snapshot,'--cli',cli,'--out',args.out/'corpus-audit'])
    if args.dry_run:
        print(json.dumps(dict(validated_inputs=True,stages=commands),indent=2));return 0
    assert all(hashlib.sha256((args.compiler_root/name).read_bytes()).hexdigest()==value for name,value in bound.items()),'Compiler input source changed during acceptance'
    print('M4 source-built compiler component acceptance passed. No prebuilt, application, GPU, or production adoption claim.')
    return 0
if __name__=='__main__':raise SystemExit(main())
