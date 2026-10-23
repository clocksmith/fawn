"""Source-bound native, CLI, guard-page and sanitizer M4 acceptance."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import all_fixtures

HERE=Path(__file__).resolve().parent

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['root','compiler-root','library','cli','validator','out']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    for key in vars(a):setattr(a,key,getattr(a,key).resolve())
    a.out.mkdir(parents=True,exist_ok=False);build=a.out/'native';build.mkdir();commands=[]
    def run(name,cmd,env=None):
        result=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=env,cwd=HERE)
        (a.out/(name+'.log')).write_text(result.stdout+result.stderr)
        commands.append(dict(name=name,command=list(map(str,cmd)),returncode=result.returncode))
        (a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name+': '+('PASS' if result.returncode==0 else 'FAIL'),flush=True)
        if result.returncode:print(result.stdout+result.stderr,flush=True)
        return result.returncode==0
    sources=[a.compiler_root/'src'/f'{name}.c' for name in ['lex','numeric','parse','validate','spirv','compile']]
    include='-I'+str(a.compiler_root/'include');flags=['-std=c11','-O2','-Wall','-Wextra','-Werror']
    sanflags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',PYTHONDONTWRITEBYTECODE='1')
    for cc,standard in [('gcc','c11'),('g++','c++17')]:
        for name in ['consumer','consumer_m2','consumer_m4']:
            binary=build/(cc+'_'+name)
            run(binary.name+'_build',[cc,'-std='+standard,'-O2','-Wall','-Wextra','-Werror',include,*(['-x','c++','-fno-exceptions','-fno-rtti'] if cc=='g++' else []),HERE/'native'/(name+'.c'),*(['-x','none'] if cc=='g++' else []),a.library,'-Wl,-rpath,'+str(a.library.parent),'-o',binary])
            run(binary.name,[binary])
    for name in ['guard_harness','guard_m2','guard_m4']:
        run(name+'_build',['gcc',*flags,include,HERE/'native'/(name+'.c'),'-ldl','-o',build/name])
        run(name,[build/name,a.library])
    sanlib=build/'libm4_sanitize.so'
    run('san_library_build',['gcc',*sanflags,'-fPIC','-shared',*sources,'-Wl,--version-script='+str(a.compiler_root/'src/exports.map'),'-o',sanlib])
    for name in ['sanitizer_fuzz','sanitizer_m2','sanitizer_m4','corpus_m2']:
        run(name+'_build',['gcc',*sanflags,include,HERE/'native'/(name+'.c'),sanlib,'-Wl,-rpath,$ORIGIN','-o',build/name])
        if name!='corpus_m2':run(name,[build/name],env)
    corpus=a.out/'frozen-corpus';corpus.mkdir();bound=[]
    for kind,cases in [('positive',all_fixtures.admitted(a.root)),('negative',all_fixtures.rejected())]:
        for case in cases:
            name,source=case[:2];path=corpus/(kind+'_'+name+'.wgsl');path.write_bytes(source);bound.append(dict(kind=kind,name=name,sha256=hashlib.sha256(source).hexdigest()))
    (corpus/'sources.json').write_text(json.dumps(bound,indent=2)+'\n')
    run('sanitizer_corpus',[build/'corpus_m2',*sorted(corpus.glob('*.wgsl'))],env)
    options=['--root',a.root,'--library',a.library,'--validator',a.validator]
    run('cli_corpus',[sys.executable,HERE/'check_cli_m4.py',*options,'--cli',a.cli,'--corpus','--out',a.out/'cli-corpus'],env)
    wrap=['-Wl,--wrap=write','-Wl,--wrap=close','-Wl,--wrap=link']
    for label,chosen in [('fault_cli',flags),('san_fault_cli',sanflags)]:
        binary=build/label
        run(label+'_build',['gcc',*chosen,*sources,a.compiler_root/'src/cli.c',HERE/'io_wrap.c',*wrap,'-o',binary])
        run(label,[sys.executable,HERE/'check_cli_m4.py',*options,'--cli',binary,'--faults','--out',a.out/(label+'-checks')],env)
    for source in [*sources,a.compiler_root/'src/cli.c']:
        run('stack_'+source.stem,['gcc',*flags,'-fstack-usage','-c',source,'-o',build/(source.stem+'.o')])
    for name,cmd in [('exports',['nm','-D','--defined-only',a.library]),('dependencies',['ldd',a.library]),('gcc_version',['gcc','--version']),('gpp_version',['g++','--version']),('python_version',[sys.executable,'--version']),('validator_version',[a.validator,'--version'])]:run(name,cmd)
    hashes={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [*sources,a.compiler_root/'src/cli.c',a.compiler_root/'include/doe_compiler.h',a.library,a.cli,a.validator,*sorted(HERE.glob('*.py'))]}
    (a.out/'source-artifact-hashes.json').write_text(json.dumps(hashes,indent=2)+'\n')
    return int(any(c['returncode'] for c in commands))
if __name__=='__main__':raise SystemExit(main())
