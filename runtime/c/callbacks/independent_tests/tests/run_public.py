#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Independent public callback/futures C/C++ tests, preserving every outcome."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import time

WRAPS = ['completion_requirements', 'new_id', 'legacy_id', 'completion_init',
         'register', 'mark_ready', 'finish', 'completion_deinit']

def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repo', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--callback-archive', type=Path)
    p.add_argument('--futures-archive', type=Path)
    p.add_argument('--mode', choices=['normal', 'asan', 'tsan'], default='normal')
    args = p.parse_args()
    repo, out = args.repo.resolve(), args.out.resolve()
    here = Path(__file__).resolve().parent
    out.mkdir(parents=True, exist_ok=False)
    commands = []
    def run(cmd, label, fatal=False, status=None):
        started = time.monotonic()
        r = subprocess.run([str(x) for x in cmd], capture_output=True, text=True,
                           timeout=120, check=False)
        record = dict(command=[str(x) for x in cmd], elapsed=time.monotonic()-started,
                      returncode=r.returncode, stdout=r.stdout, stderr=r.stderr,
                      max_rss_kib=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)
        (out/(label+'.json')).write_text(json.dumps(record, indent=2)+'\n')
        commands.append(record)
        print(label, r.returncode, flush=True)
        if fatal:
            assert r.returncode == -6, record
            assert re.search(r'C callbacks .*failed closed: status '+str(status)+r'\b', r.stderr), record
        else:
            assert r.returncode == 0, record
        return r
    flags = ['-O2' if args.mode=='normal' else '-O1', '-g', '-fPIC', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-pthread']
    if args.mode != 'normal':
        flags += ['-fno-omit-frame-pointer', '-fno-sanitize-recover=all', '-fsanitize='+('address,undefined' if args.mode=='asan' else 'thread')]
    include = ['-I', repo/'runtime/c/callbacks/include', '-I', repo/'runtime/c/futures/include']
    archives = []
    for owner, supplied in [('callbacks', args.callback_archive), ('futures', args.futures_archive)]:
        if supplied:
            archive = supplied.resolve()
            assert archive.is_file()
        else:
            sources = sorted((repo/f'runtime/c/{owner}/src').glob('*.c'))
            assert sources
            objects = []
            for index, source in enumerate(sources):
                obj = out/(owner+'-'+source.stem+'.o')
                run(['gcc','-std=c11',*flags,*include,'-c',source,'-o',obj], f'build-{owner}-{index}')
                objects.append(obj)
            archive = out/('libdoe_'+owner+'.a')
            run(['ar','rcs',archive,*objects], 'archive-'+owner)
        archives.append(archive)
        run(['nm','-g','--defined-only',archive], 'symbols-'+owner)
        run(['ar','t',archive], 'members-'+owner)
    sources = [*here.glob('*.c'), *here.glob('*.cpp')]
    (out/'inputs.sha256').write_text(''.join(hashlib.sha256(x.read_bytes()).hexdigest()+'  '+str(x)+'\n' for x in [*sources,*archives]))
    wraps = ['-Wl,--wrap=doe_future_'+name for name in WRAPS]
    c = out/'public_c'
    run(['gcc','-std=c11',*flags,*include,here/'callback_api.c',*archives,*wraps,'-o',c], 'link-c')
    run([c], 'run-c')
    cpp = out/'public_cpp'
    run(['g++','-std=c++17','-fno-exceptions','-fno-rtti',*flags,*include,here/'callback_cpp.cpp',*archives,'-o',cpp], 'link-cpp')
    run([cpp], 'run-cpp')
    if args.mode=='normal':
        for name in ['requirements','new_id','legacy_id','init','register','mark_ready','finish','deinit']:
            run([c,name], 'fatal-'+name, True, 9 if name in ['new_id','legacy_id'] else 10)
        for name in ['mark_ready','finish']:
            run([c,name,'post'], 'fatal-'+name+'-post', True, 10)
        # Misaligned allocation is a precondition violation but must abort with
        # the frozen diagnostic envelope, not fabricate recoverable transfer.
        r = subprocess.run([str(c),'misaligned'], capture_output=True, text=True)
        (out/'fatal-misaligned.json').write_text(json.dumps(dict(returncode=r.returncode,stdout=r.stdout,stderr=r.stderr),indent=2)+'\n')
        assert r.returncode==-6 and 'C callbacks' in r.stderr and 'failed closed' in r.stderr
    (out/'summary.json').write_text(json.dumps(dict(mode=args.mode,commands=len(commands),passed=True),indent=2)+'\n')
    return 0
if __name__=='__main__':
    raise SystemExit(main())
