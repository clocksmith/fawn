"""Audit each frozen discovery source with unique outputs, without gate main."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True
import fixtures
from typed_oracle import Module
from semantic_checks import run_vectors
import struct

def sha(p: Path) -> str:return hashlib.sha256(p.read_bytes()).hexdigest()
def main() -> int:
    p=argparse.ArgumentParser()
    for n in ['root','cli','validator','frozen-inputs','out']:p.add_argument('--'+n,type=Path,required=True, help='Explicit input or fresh output for this acceptance stage.')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    spec=importlib.util.spec_from_file_location('gate',a.root/'bench/gates/spirv_val_gate.py')
    gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
    bound=json.loads(a.frozen_inputs.read_text(encoding='utf-8'));sources=gate.discover_wgsl_files(gate.DEFAULT_WGSL_DISCOVERY_DIRS)
    assert len(sources)==98
    assert {str(x.relative_to(a.root)):sha(x) for x in sources}=={x['path']:x['sha256'] for x in bound['wgsl']}
    assert sha(a.root/'bench/gates/spirv_val_gate.py')=='f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    expected={raw:expect for _,raw,expect in fixtures.admitted(a.root)}
    rows=[];trials=0
    for i,source in enumerate(sources):
        output=a.out/f'{i:03}_{source.stem}.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(a.cli),source,output)
        row=dict(source=str(source.relative_to(a.root)),source_sha256=sha(source),admitted=ok,message=message)
        if ok:
            run=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(output)],capture_output=True,text=True)
            row.update(output_sha256=sha(output),validator_returncode=run.returncode,validator_stdout=run.stdout,validator_stderr=run.stderr)
            raw=output.read_bytes();expect=expected[source.read_bytes()]
            module=Module(list(struct.unpack('<'+'I'*(len(raw)//4),raw)))
            module.interface(expect);count=run_vectors(module,expect);trials+=count;row['cpu_trials']=count
        rows.append(row)
    preserved=all(sha(a.root/x['path'])==x['sha256'] for x in bound['wgsl']+bound['prebuilt'])
    summary=dict(cli_sha256=sha(a.cli),consumer_sha256=sha(a.root/'bench/gates/spirv_val_gate.py'),validator_sha256=sha(a.validator),
        sources=len(rows),admitted=sum(r['admitted'] for r in rows),rejected=sum(not r['admitted'] for r in rows),
        vulkan11_passed=sum(r.get('validator_returncode')==0 for r in rows),cpu_trials=trials,all_bound_inputs_preserved=preserved,
        records=rows,note='Independent unique-path consumer calls; not another full gate invocation.')
    (a.out/'results.json').write_text(json.dumps(summary,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in summary.items() if k!='records'},indent=2, sort_keys=True))
    return int(not preserved or any(r.get('validator_returncode',0) for r in rows))
if __name__=='__main__':raise SystemExit(main())
