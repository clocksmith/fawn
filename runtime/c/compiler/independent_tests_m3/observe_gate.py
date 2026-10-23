"""Exactly one unchanged discovery-gate observation and separate unique-path audit."""
from __future__ import annotations
import argparse
import collections
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
sys.dont_write_bytecode=True

def sha(p: Path) -> str: return hashlib.sha256(p.read_bytes()).hexdigest()
def main() -> int:
    p=argparse.ArgumentParser()
    for n in ['root','cli','validator','out']:p.add_argument('--'+n,type=Path,required=True, help='Explicit input or fresh output for this acceptance stage.')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    temp=a.out/'tmp';temp.mkdir()
    os.environ['TMPDIR']=str(temp);os.environ['PYTHONDONTWRITEBYTECODE']='1'
    spec=importlib.util.spec_from_file_location('real_gate',a.root/'bench/gates/spirv_val_gate.py')
    gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
    assert sha(a.root/'bench/gates/spirv_val_gate.py')=='f715496e278e6c2fbdad94f4ce9e2a29b8b67095f933990f2568ec9c6237677b'
    wgsl=gate.discover_wgsl_files(gate.DEFAULT_WGSL_DISCOVERY_DIRS)
    spvs=gate.collect_spv_files(gate.DEFAULT_SPV_SEARCH_DIRS)
    assert len(wgsl)==98 and len(spvs)==36
    bind=lambda ps:[dict(path=str(p.relative_to(a.root)),sha256=sha(p),bytes=p.stat().st_size) for p in ps]
    before=bind(wgsl+spvs)
    groups=collections.defaultdict(list)
    for source in wgsl:groups[source.stem].append(str(source.relative_to(a.root)))
    frozen=dict(wgsl=bind(wgsl),prebuilt=bind(spvs),duplicate_basenames={k:v for k,v in groups.items() if len(v)>1},
        cli_sha256=sha(a.cli),validator_sha256=sha(a.validator),consumer_sha256=sha(a.root/'bench/gates/spirv_val_gate.py'))
    (a.out/'before.json').write_text(json.dumps(frozen,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    command=[sys.executable,str(a.root/'bench/gates/spirv_val_gate.py'),'--discover-wgsl','--emit-spirv-bin',str(a.cli),
        '--spirv-val',str(a.validator),'--require','--require-subgroup-coverage','--json-report',str(a.out/'gate-report.json')]
    (a.out/'command.json').write_text(json.dumps(dict(command=command,environment=dict(TMPDIR=str(temp),PYTHONDONTWRITEBYTECODE='1')),indent=2, sort_keys=True)+'\n', encoding='utf-8')
    run=subprocess.run(command,capture_output=True,text=True,env=os.environ)
    (a.out/'gate.stdout').write_text(run.stdout, encoding='utf-8');(a.out/'gate.stderr').write_text(run.stderr, encoding='utf-8')
    (a.out/'gate-exit.json').write_text(json.dumps(dict(returncode=run.returncode), sort_keys=True)+'\n', encoding='utf-8')
    # A different, explicitly labelled pass, not a second invocation of gate main.
    # Unique destinations avoid its repeated-basename collision.
    audit=a.out/'unique-source-audit';audit.mkdir();records=[]
    for i,source in enumerate(wgsl):
        output=audit/f'{i:03}_{source.stem}.spv'
        ok,message=gate.emit_spirv_for_wgsl(str(a.cli),source,output)
        record=dict(source=str(source.relative_to(a.root)),source_sha256=sha(source),admitted=ok,message=message,
            output=str(output.relative_to(a.out)) if output.exists() else None)
        if ok:
            check=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(output)],capture_output=True,text=True)
            record.update(output_sha256=sha(output),bytes=output.stat().st_size,vulkan11_returncode=check.returncode,
                          validator_stdout=check.stdout,validator_stderr=check.stderr)
        records.append(record)
    after=bind(wgsl+spvs)
    summary=dict(gate_exit=run.returncode,gate_report=json.loads((a.out/'gate-report.json').read_text(encoding='utf-8')),
        independent_unique_source_audit=dict(sources=len(records),admitted=sum(x['admitted'] for x in records),
        rejected=sum(not x['admitted'] for x in records),vulkan11_passed=sum(x.get('vulkan11_returncode')==0 for x in records),
        vulkan11_failed=sum(x.get('vulkan11_returncode',0)!=0 for x in records)),
        all_inputs_preserved=before==after,temp_residue=list(map(str,temp.iterdir())),
        limitations=['Gate EMIT-SKIP is nonfatal.','Gate validator invocation omits explicit Vulkan1.1 target.',
        'Gate subgroup option does not make skipped subgroup sources fatal.','Duplicate basenames share gate temporary paths.',
        'Prebuilt artifacts are not attributed to this compiler.','Separate unique-source audit establishes admitted source count, not semantic correctness for previously un-oracled sources.'])
    (a.out/'audit-records.json').write_text(json.dumps(records,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    (a.out/'summary.json').write_text(json.dumps(summary,indent=2, sort_keys=True)+'\n', encoding='utf-8')
    print(json.dumps(summary,indent=2, sort_keys=True))
    return int(not summary['all_inputs_preserved'] or bool(summary['temp_residue']) or summary['independent_unique_source_audit']['vulkan11_failed'])
if __name__=='__main__':raise SystemExit(main())
