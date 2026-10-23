#!/usr/bin/env python3
"""Execute the existing ASan artifact with runtime first, then source-build TSan."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
from monitored_build_v2 import run_build

BASE=Path('/tmp/doe-instance-evaluator-0eyngbi0')
EVIDENCE=Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
RECEIPTS=EVIDENCE/'native-sanitizers-final'


def main() -> int:
    RECEIPTS.mkdir(exist_ok=False)
    environment=os.environ.copy()
    environment.update(DOE_BACKEND='metal',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1',TSAN_OPTIONS='halt_on_error=1')
    asan_library=Path(subprocess.check_output(['gcc','-print-file-name=libasan.so'],text=True).strip()).resolve()
    artifact=BASE/'native-sanitizers/asan/independent-instance'
    metadata={'source_build_receipt':'native-sanitizers-repair1/asan.json','existing_source_built_artifact':str(artifact),'sha256':hashlib.sha256(artifact.read_bytes()).hexdigest(),'runtime':str(asan_library),'runtime_sha256':hashlib.sha256(asan_library.read_bytes()).hexdigest(),'LD_PRELOAD_scope':'Only the actual independently source-built test binary, not compiler/build process.','leak_sanitizer':False,'retained_zig_instrumented':False}
    (RECEIPTS/'asan-binding.json').write_text(json.dumps(metadata,indent=2)+'\n')
    env=environment.copy();env['LD_PRELOAD']=str(asan_library)
    row=run_build('asan-runtime-first',[str(artifact),'--seed=0x3b161b60'],BASE/'native-independent',env,RECEIPTS)
    assert row['returncode']==0 and not row['timed_out'] and 'All 12 tests passed.' in row['stderr'],row
    original=json.loads((EVIDENCE/'native-independent/native-focused-fixtures-repair1.json').read_text())
    command=original['argv'].copy()
    old=[BASE/'instance-normal/libdoe_instance.a',BASE/'callbacks-developer/libdoe_callbacks.a',BASE/'accepted-futures/libdoe_futures_zig.a']
    root=BASE/'native-sanitizers/tsan'
    new=[root/'instance/libdoe_instance.a',root/'callbacks/libdoe_callbacks.a',root/'futures/libdoe_futures_zig.a']
    for index,arg in enumerate(command):
        for before,after in zip(old,new):
            if arg==str(before):command[index]=str(after)
        if arg=='--cache-dir':command[index+1]=str(root/'zig-cache')
        if arg=='--global-cache-dir':command[index+1]=str(BASE/'native-independent/global-cache')
        if arg=='--name':command[index+1]='independent_instance_tsan'
        if arg.startswith('-femit-bin='):command[index]='-femit-bin='+str(root/'independent-instance')
    tsan=Path(subprocess.check_output(['gcc','-print-file-name=libtsan.so'],text=True).strip())
    command+=['-L',str(tsan.parent),'-ltsan','--test-no-exec']
    row=run_build('tsan-source-build',command,Path(original['cwd']),environment,RECEIPTS)
    assert row['returncode']==0 and not row['timed_out'],row
    row=run_build('tsan-runtime',[str(root/'independent-instance')],Path(original['cwd']),environment,RECEIPTS)
    assert row['returncode']==0 and not row['timed_out'] and 'All 12 tests passed.' in row['stderr'],row
    (RECEIPTS/'summary.json').write_text(json.dumps({'passed':True,'tests_each':12,'modes':['ASan+UBSan','TSan'],'instrumented_objects':['instance.c','wait.c','capabilities.c','callbacks.c','storage.c','futures.c','doe_futures_zig_bridge.c'],'not_instrumented':['retained Zig consumers/fixtures','Metal/D3D12 C stubs','system libraries'],'leak_sanitizer':False},indent=2)+'\n')
    return 0


if __name__=='__main__':raise SystemExit(main())
