#!/usr/bin/env python3
"""Compile selected compute/full roots; do not execute their artifacts."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
from monitored_build import run_build

BASE=Path('/tmp/doe-instance-evaluator-0eyngbi0')
EVIDENCE=Path('/workspace/shared/doe-c-migration-evidence-20261023/instance1/evaluator')
WORK=BASE/'native-fci-retry1'
RECEIPTS=EVIDENCE/'native-named-roots'


def main() -> int:
    RECEIPTS.mkdir(exist_ok=False)
    original=json.loads((EVIDENCE/'native-fci-retry1/FCI-canonical-fresh-retry1.json').read_text())
    argv=original['argv'].copy()
    argv[2:3]=['dropin-compute','dropin-full']
    argv+=['--summary','all']
    environment=os.environ.copy();environment.update(original['environment'])
    result=run_build('FCI-compute-full-compile-only',argv,Path(original['cwd']),environment,RECEIPTS)
    assert not result['timed_out'] and result['returncode']==0,result
    artifacts=[]
    for name in ['libwebgpu_doe_compute.so','libwebgpu_doe_full.so']:
        p=WORK/'install/lib'/name
        artifacts.append({'path':str(p),'size_bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'executed':False})
    options=[]
    for p in (WORK/'cache').rglob('options.zig'):
        text=p.read_text()
        if 'experimental_c_instance' not in text:continue
        for flag in ['instance','callbacks','futures']:
            assert f'pub const experimental_c_{flag}: bool = true;' in text
        digest=hashlib.sha256(p.read_bytes()).hexdigest()
        target=RECEIPTS/('options-'+digest+'.zig');target.write_text(text)
        options.append(str(target))
    (RECEIPTS/'summary.json').write_text(json.dumps({'passed':True,'compile_only':True,'artifacts':artifacts,'options':options,'all_36_configured_artifact_roots_executed':False},indent=2)+'\n')
    return 0


if __name__=='__main__':raise SystemExit(main())
