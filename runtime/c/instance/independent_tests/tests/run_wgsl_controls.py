#!/usr/bin/env python3
"""Compare unchanged public WGSL exports across source and prebuilt providers."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

BASE = Path('/tmp/doe-instance-evaluator-0eyngbi0')
EVIDENCE = Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
RECEIPTS = EVIDENCE / 'native-wgsl-controls'
WORK = BASE / 'native-wgsl-controls'


def main() -> int:
    RECEIPTS.mkdir(exist_ok=False)
    WORK.mkdir(exist_ok=False)
    rows = []
    environment = os.environ.copy()
    environment.update(DOE_BACKEND='metal', LD_LIBRARY_PATH='/workspace/shared/doe-language-experiment-20261023/phase1/system-link-prefix/lib')
    def run(label: str, argv: list[str]) -> str:
        start = time.monotonic()
        result = subprocess.run(argv, capture_output=True, text=True, env=environment, timeout=120)
        row = {'argv': argv, 'returncode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr, 'elapsed_seconds': time.monotonic()-start}
        (RECEIPTS/(label+'.json')).write_text(json.dumps(row,indent=2)+'\n')
        rows.append(row)
        assert result.returncode == 0, row
        return result.stdout
    for compiler in ['gcc','g++']:
        command = [compiler, '-std=c11' if compiler=='gcc' else '-std=c++17', '-Wall','-Wextra','-Werror','-pedantic','-O2']
        if compiler=='g++':command += ['-fno-exceptions','-fno-rtti']
        command += ['-I',str(BASE/'native-source/runtime/zig/vendor/webgpu-headers'),str(EVIDENCE/'tests/native_wgsl_control.c'),'-ldl','-o',str(WORK/compiler)]
        run('build-'+compiler,command)
        for provenance in ['source-built','author-prebuilt']:
            baseline = None
            for lane in ['Z','F','FC','FCI']:
                if provenance=='author-prebuilt':library=Path('/tmp/doe-instance-author-20261023-final')/lane/'lib/libwebgpu_doe.so'
                elif lane=='FCI':library=BASE/'native-fci-retry1/install/lib/libwebgpu_doe.so'
                else:library=BASE/'native-controls-complete'/(lane+'-install/lib/libwebgpu_doe.so')
                observations=json.loads(run(provenance+'-'+lane+'-'+compiler,[str(WORK/compiler),str(library)]))
                assert observations[0]==observations[1]==observations[2], observations
                if lane=='Z':baseline=observations
                else:assert observations==baseline,(provenance,lane,observations,baseline)
                (RECEIPTS/(provenance+'-'+lane+'-artifact.json')).write_text(json.dumps({'path':str(library),'sha256':hashlib.sha256(library.read_bytes()).hexdigest()},indent=2)+'\n')
    (RECEIPTS/'summary.json').write_text(json.dumps({'passed':True,'commands':len(rows),'distinct_provenance':['source-built','author-prebuilt'],'direct_explicit_and_sentinel_proc_equal':True},indent=2)+'\n')
    print('WGSL controls PASS',len(rows),flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
