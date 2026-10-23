#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Portable independent real native owner tests and fresh source artifact build."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--zig', type=Path, required=True)
    p.add_argument('--loader-prefix', type=Path, required=True)
    p.add_argument('--source-manifest', type=Path, required=True)
    p.add_argument('--callback-archive', type=Path, required=True)
    p.add_argument('--futures-archive', type=Path, required=True)
    p.add_argument('--build-shared', action='store_true')
    p.add_argument('--native-library', type=Path)
    p.add_argument('--mode', choices=['normal','asan','tsan'], default='normal')
    p.add_argument('--prepare-only', action='store_true')
    a = p.parse_args()
    here=Path(__file__).resolve().parent
    repo,out,zig=a.repo.resolve(),a.out.resolve(),a.zig.resolve()
    out.mkdir(parents=True,exist_ok=False)
    copied=out/'source'
    paths=[]
    for row in a.source_manifest.read_text().splitlines():
        digest,name=row.split(maxsplit=1)
        rel=Path(name)
        assert not rel.is_absolute() and '..' not in rel.parts
        source=repo/rel
        assert hashlib.sha256(source.read_bytes()).hexdigest()==digest, name
        destination=copied/rel
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,destination)
        paths.append(name)
    runtime=copied/'runtime/zig'
    shutil.copy2(here/'native_cases.zig',runtime/'independent_callbacks.zig')
    shutil.copy2(here/'build_options.fixture.zig',runtime/'options.zig')
    futures=copied/'runtime/c/futures'
    callbacks=copied/'runtime/c/callbacks'
    archive_args=[str(a.callback_archive.resolve()),str(a.futures_archive.resolve())]
    includes=[]
    for d in [runtime/'src/backend/d3d12',runtime/'src/backend/metal',runtime/'vendor/webgpu-headers',futures/'include',futures/'integration',callbacks/'include']:
        includes+=['-I',str(d)]
    command=[str(zig),'test',*archive_args,str(runtime/'src/backend/d3d12/d3d12_bridge_stubs.c'),'-lvulkan',str(runtime/'src/backend/metal/metal_bridge_stubs.c'),'-OReleaseFast',*includes,'--dep','build_options','-Mroot='+str(runtime/'independent_callbacks.zig'),'-Mbuild_options='+str(runtime/'options.zig'),'-lc','--test-filter','independent callbacks:','--cache-dir',str(out/'cache'),'--global-cache-dir',str(out/'global-cache'),'--name','independent_callbacks','-L',str(a.loader_prefix.resolve()/'lib'),'-femit-bin='+str(out/'independent_callbacks')]
    if a.mode!='normal':
        sanitizer='asan' if a.mode=='asan' else 'tsan'
        lib=Path(subprocess.check_output(['gcc','-print-file-name=lib'+sanitizer+'.so'],text=True).strip())
        assert lib.is_file()
        command+=['-L',str(lib.parent),'-l'+sanitizer]
        if a.mode=='asan':command+=['-lubsan']
    plan={'verified_sources':paths,'commands':[command],'independent_fixture_sha256':hashlib.sha256((here/'native_cases.zig').read_bytes()).hexdigest()}
    (out/'plan.json').write_text(json.dumps(plan,indent=2)+'\n')
    def run(cmd,label,cwd=out):
        print(label,flush=True)
        if a.prepare_only:return
        started=time.monotonic()
        result=subprocess.run([str(x) for x in cmd],cwd=cwd,capture_output=True,text=True,timeout=600,check=False)
        record=dict(command=[str(x) for x in cmd],cwd=str(cwd),returncode=result.returncode,elapsed_seconds=time.monotonic()-started,stdout=result.stdout,stderr=result.stderr)
        (out/(label+'.json')).write_text(json.dumps(record,indent=2)+'\n')
        assert result.returncode==0,record
    run(command,'independent-owner-tests')
    library=a.native_library.resolve() if a.native_library else None
    if a.build_shared:
        assert a.mode=='normal'
        # Canonical source build, not consumption of the author's native binary.
        # Exact callback/futures archive inputs are independently supplied/hashed.
        command=[zig,'build','dropin','-Doptimize=ReleaseFast','-Dexperimental-c-callbacks-archive='+str(a.callback_archive.resolve()),'-Dexperimental-c-futures-archive='+str(a.futures_archive.resolve()),'--search-prefix',a.loader_prefix.resolve(),'--cache-dir',out/'shared-cache','--global-cache-dir',out/'shared-global-cache','--prefix',out/'install']
        run(command,'canonical-source-build',copied/'runtime/zig')
        library=out/'install/lib/libwebgpu_doe.so'
    if library:
        for compiler,lang,extra in [('gcc','c11',[]),('g++','c++17',['-x','c++','-fno-exceptions','-fno-rtti'])]:
            exe=out/('native-'+compiler)
            run([compiler,*extra,'-std='+lang,'-O2','-Wall','-Wextra','-Wpedantic','-Werror','-pthread','-I',runtime/'vendor/webgpu-headers',here/'native_consumer.c','-ldl','-o',exe],'compile-'+compiler)
            run([exe,library],'run-'+compiler)
        if not a.prepare_only:
            for family in ['device','map','scope','info','compute','render']:
                for mode in [1,2,9]:
                    cmd=[str(out/'native-gcc'),str(library),family,str(mode)]
                    r=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
                    row=dict(command=cmd,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr)
                    (out/(f'fatal-{family}-{mode}.json')).write_text(json.dumps(row,indent=2)+'\n')
                    assert r.returncode==-6 and ('UnsupportedCallbackMode' if mode==9 else 'CallbackInstanceUnavailable') in r.stderr,row
    return 0
if __name__=='__main__':raise SystemExit(main())
