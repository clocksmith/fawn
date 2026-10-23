"""Source-only M3 acceptance; retains unmodified M2 checks as a named stage."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
FLAGS=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror']
LIB_NAMES=['lex.c','parse.c','validate.c','spirv.c','compile.c']
LIB_HASH='38281d1e2c2842a925915ad6ab6dea7ddd332f86c381e43561b68b99080f63cc'
def digest(p: Path) -> str:return hashlib.sha256(p.read_bytes()).hexdigest()
def main() -> int:
    p=argparse.ArgumentParser()
    for name in ['repo-root','compiler-root','prebuilt-cli','prebuilt-library','validator','out']:
        p.add_argument('--'+name,type=Path,required=True, help='Explicit input or fresh output for this acceptance stage.')
    a=p.parse_args()
    for key,val in vars(a).items():setattr(a,key,val.resolve())
    a.out.mkdir(parents=True,exist_ok=False)
    logs=a.out/'logs';logs.mkdir();tmp=a.out/'tmp';tmp.mkdir()
    os.environ.update(TMPDIR=str(tmp),PYTHONDONTWRITEBYTECODE='1')
    env=dict(os.environ)
    commands=[]
    def run(name: str, cmd: list[str | Path], extra: dict[str, str] | None = None) -> int:
        result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=dict(env,**(extra or {})))
        (logs/(name+'.stdout')).write_text(result.stdout, encoding='utf-8')
        (logs/(name+'.stderr')).write_text(result.stderr, encoding='utf-8')
        commands.append(dict(name=name,command=list(map(str,cmd)),environment={k:env[k] for k in ['TMPDIR','PYTHONDONTWRITEBYTECODE']},extra_environment=extra,returncode=result.returncode))
        (a.out/'commands.json').write_text(json.dumps(commands,indent=2, sort_keys=True)+'\n', encoding='utf-8')
        print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
        if result.returncode:print(result.stdout+result.stderr,flush=True)
        return result.returncode
    snap=a.out/'snapshot';snap.mkdir()
    shutil.copytree(a.compiler_root/'src',snap/'src')
    shutil.copytree(a.compiler_root/'include',snap/'include')
    for name in ['MILESTONE_1.md','MILESTONE_2.md','MILESTONE_3.md','CONTRACT.sha256','CONTRACT_2.sha256','CONTRACT_3.sha256']:
        shutil.copy2(a.compiler_root/name,snap/name)
    expected_sources=json.loads((HERE/'LIBRARY_SOURCE_IDENTITY.json').read_text(encoding='utf-8'))
    for name,identity in expected_sources.items():
        assert digest(snap/'src'/name)==identity['sha256'], 'M2 library source changed: '+name
    prebuilt=a.out/'prebuilt';prebuilt.mkdir()
    shutil.copy2(a.prebuilt_cli,prebuilt/'doe-compiler-c')
    shutil.copy2(a.prebuilt_library,prebuilt/'libdoe_compiler.so')
    assert digest(prebuilt/'libdoe_compiler.so')==LIB_HASH
    assert digest(snap/'include/doe_compiler.h')=='5bf4f5e5a4e4624c094320ad9ec97119278bc7f6f06b0e47c01e444d42c6dc5b'
    assert digest(snap/'MILESTONE_3.md')=='7b30c21d3c6451f7ce21a1743ea01860452af09454a1f6b188def4ed3e995d16'
    assert digest(a.repo_root/'bench/gates/spirv_val_gate.py')=='f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    bound=[*sorted(snap.rglob('*')),*sorted(prebuilt.iterdir()),a.validator,*sorted(HERE.rglob('*'))]
    bound=[x for x in bound if x.is_file() and '__pycache__' not in x.parts]
    (a.out/'source-artifact-bindings.json').write_text(json.dumps({str(x):digest(x) for x in bound},indent=2, sort_keys=True)+'\n', encoding='utf-8')
    failures=[]
    for tool in ['gcc','g++','python3']:
        if run(tool+'-version',[tool,'--version']):failures.append(tool+'-version')
    run('validator-version',[a.validator,'--version'])
    tool_paths={name:Path(shutil.which(name)).resolve() for name in ['gcc','g++','python3','make','nm','ldd']}
    for name in ['cc1','cc1plus','as','ld']:
        value=subprocess.run(['gcc','-print-prog-name='+name],capture_output=True,text=True,check=True).stdout.strip()
        tool_paths[name]=Path(shutil.which(value) or value).resolve()
    (a.out/'tool-hashes.json').write_text(json.dumps({name:dict(path=str(path),sha256=digest(path)) for name,path in tool_paths.items()},indent=2, sort_keys=True)+'\n', encoding='utf-8')
    fresh=a.out/'source-build';fresh.mkdir()
    sources=[snap/'src'/x for x in LIB_NAMES]
    builds=[('cli-build',['gcc',*FLAGS,*sources,snap/'src/cli.c','-o',fresh/'doe-compiler-c']),
        ('library-build',['gcc',*FLAGS,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(snap/'src/exports.map'),'-o',fresh/'libdoe_compiler.so'])]
    for name,cmd in builds:
        if run(name,cmd):return 1
    identity={'library_identical':digest(fresh/'libdoe_compiler.so')==LIB_HASH,
        'cli_source_prebuilt_identical':digest(fresh/'doe-compiler-c')==digest(prebuilt/'doe-compiler-c'),
        'artifacts':{str(x.relative_to(a.out)):dict(sha256=digest(x),bytes=x.stat().st_size) for d in [fresh,prebuilt] for x in d.iterdir()}}
    (a.out/'build-identity.json').write_text(json.dumps(identity,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    if not identity['library_identical'] or not identity['cli_source_prebuilt_identical']:failures.append('artifact identity')
    for d in [fresh,prebuilt]:
        for name in ['doe-compiler-c','libdoe_compiler.so']:
            run(d.name+'-'+name+'-dependencies',['ldd',d/name])
    run('exports',['nm','-D','--defined-only',fresh/'libdoe_compiler.so'])
    common=['--root',a.repo_root,'--validator',a.validator]
    for name,d,corpus in [('source-cli',fresh,True),('prebuilt-cli',prebuilt,False)]:
        cmd=[sys.executable,HERE/'m2/check_cli_m3.py',*common,'--cli',d/'doe-compiler-c','--library',d/'libdoe_compiler.so','--out',a.out/name]
        if corpus:cmd+=['--corpus']
        if run(name,cmd):failures.append(name)
    # This exact unchanged M2 suite includes trailing --reflection as a required
    # missing-value rejection; it has no bare omission rejection to disable.
    if run('retained-m2',[sys.executable,HERE/'m2/run_acceptance.py','--repo-root',a.repo_root,
        '--compiler-root',snap,'--library',fresh/'libdoe_compiler.so','--cli',fresh/'doe-compiler-c',
        '--validator',a.validator,'--out',a.out/'retained-m2']):failures.append('retained-m2')
    wraps=['-Wl,--wrap='+name for name in ['write','close','link']]
    for name,sanitize in [('wrapped-cli',False),('sanitized-cli',True)]:
        flags=FLAGS+(['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined'] if sanitize else [])
        if run(name+'-build',['gcc',*flags,*sources,snap/'src/cli.c',HERE/'io_wrap.c',*wraps,'-o',fresh/name]):
            failures.append(name+'-build');continue
        extra=dict(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1') if sanitize else {}
        if run(name,[sys.executable,HERE/'m2/check_cli_m3.py',*common,'--cli',fresh/name,'--library',fresh/'libdoe_compiler.so','--out',a.out/name,'--faults'],extra):failures.append(name)
    summary=dict(passed=not failures,failures=failures,identity=identity,
        notes=['Source-built and separately copied prebuilt are distinct stages.','No full gate observation in this reusable runner; the evaluator owns one separate observation.','ASan/UBSan used; LeakSanitizer disabled. No physical GPU or application qualification.'])
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(summary,indent=2, sort_keys=True))
    return int(bool(failures))
if __name__=='__main__':raise SystemExit(main())
