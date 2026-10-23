#!/usr/bin/env python3
"""Opt-in binary-only ABI4 CLI, old sidecars and owned-publication fault probes."""
from __future__ import annotations
import argparse,hashlib,json,os,re,struct,subprocess,tempfile
from pathlib import Path
from fixtures_m10 import all_cases
from extra_m10 import witnesses
from run_m10 import execute
from seal_m10 import verify_seal

WRAPS=('main','malloc','calloc','realloc','free','write','read','close','link','mkstemp','lstat','doe_compiler_workspace_requirements','doe_compiler_compile','doe_compiler_compile_v2','doe_compiler_typed_requirements','doe_compiler_compile_typed')

def build_wrapped(snapshot:Path,archive:Path,output:Path,cc='cc'):
    if output.exists():raise ValueError('fresh absent output required')
    command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-fno-fast-math','-ffp-contract=off','-frounding-math','-I'+str(snapshot/'include'),str(snapshot/'src/cli.c'),str(snapshot/'src/cli_json.c'),str(Path(__file__).with_name('cli_fault_m10.c')),str(archive),*[f'-Wl,--wrap={name}' for name in WRAPS],'-lm','-o',str(output)]
    result=subprocess.run(command,capture_output=True,text=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return command

def invoke(binary,source,mode=None,fault=None,existing=False,extra=()):
    with tempfile.TemporaryDirectory(prefix='doe-m10-cli-',dir='/tmp') as temporary:
        root=Path(temporary);shader=root/'shader.wgsl';target=root/'shader.spv';side=root/'shader.json';shader.write_text(source)
        if existing:target.write_bytes(b'PREEXISTING')
        command=[str(binary),'--shader-path',str(shader),'--out',str(target),*(str(root/x) if x=='bad.json' else x for x in extra)]
        if mode:command += [mode,str(side)]
        env=dict(os.environ)
        if fault:env.update(M5_FAULT_OPERATION=fault[0],M5_FAULT_CALL=str(fault[1]))
        p=subprocess.run(command,capture_output=True,text=True,env=env)
        files={x.name:x.read_bytes() for x in root.iterdir() if x!=shader}
        trace={}
        for prefix in ('M5_TRACE','M10_TYPED'):
            rows=[line[len(prefix)+1:] for line in p.stderr.splitlines() if line.startswith(prefix+' ')]
            if rows:trace[prefix]=json.loads(rows[-1])
        return dict(status=p.returncode,stdout=p.stdout,stderr=p.stderr,files=files,trace=trace)

def run(binary,wrapped=None):
    aggregate=next(c for c in witnesses() if c['name']=='real_project_3x2_full')
    scalar=next(c for c in all_cases() if c['name']=='vector_pieces_u32_2')
    rows=[];ok=invoke(binary,aggregate['source']);assert ok['status']==0,ok
    data=ok['files']['shader.spv'];assert len(data)%4==0
    execute(aggregate,list(struct.unpack('<'+'I'*(len(data)//4),data)))
    assert set(ok['files'])=={'shader.spv'};rows.append('binary-only ABI4 project execution, no sidecar')
    for mode in ('--reflection','--reflection-v2'):
        p=invoke(binary,aggregate['source'],mode);assert p['status']!=0 and not p['files'],p
        p=invoke(binary,scalar['source'],mode);assert p['status']==0,p
        obj=json.loads(p['files']['shader.json']);assert obj['entry_point']=='main' and obj['binding_count']==1
        assert set(p['files'])=={'shader.spv','shader.json'}
        rows.append(mode+' preserved projection and no aggregate misrepresentation')
    p=invoke(binary,aggregate['source'],existing=True);assert p['status']!=0 and p['files']=={'shader.spv':b'PREEXISTING'},p
    p=invoke(binary,aggregate['source'],extra=('--reflection-v4','bad.json'));assert p['status']!=0 and not p['files'],p
    rows.append('no-clobber and no unselected JSON4 surface')
    faults=[]
    if wrapped:
        baseline=invoke(wrapped,aggregate['source']);assert baseline['status']==0,baseline
        t=baseline['trace']['M5_TRACE'];q=baseline['trace']['M10_TYPED']
        assert t['live']==0 and q['queries']>=1 and q['calls']==2 and t['abi1_calls']==0 and t['abi2_calls']==0
        assert baseline['files']==ok['files']
        mapping={'allocation':'allocations','read':'reads','write':'writes','close':'closes','link':'links','mkstemp':'temps'}
        counts={k:t[v] for k,v in mapping.items()};counts.update(typed_query=q['queries'],typed_compile=q['calls'])
        for operation,count in counts.items():
            for ordinal in range(1,count+1):
                p=invoke(wrapped,aggregate['source'],fault=(operation,ordinal));tr=p['trace']['M5_TRACE']
                assert tr['faults']==1 and tr['live']==0 and p['status']!=0 and not p['files'],(operation,ordinal,p)
                faults.append((operation,ordinal,'failed_cleanly'))
        for operation in ('read_eintr','read_short','write_eintr','write_short'):
            p=invoke(wrapped,aggregate['source'],fault=(operation,1));tr=p['trace']['M5_TRACE']
            assert tr['faults']==1 and tr['live']==0 and p['status']==0 and p['files']==ok['files'],(operation,p)
            faults.append((operation,1,'recovered'))
        for operation in ('publish_collision','unowned_temp'):
            p=invoke(wrapped,aggregate['source'],fault=(operation,1));tr=p['trace']['M5_TRACE']
            assert tr['faults']==1 and tr['live']==0 and p['status']!=0,p
            assert len(p['files'])==1 and next(iter(p['files'].values()))==b'UNOWNED',p
            faults.append((operation,1,'unowned_preserved'))
    return dict(format='m10-cli-v1',binary_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),checks=rows,faults=faults,faults_status='run' if wrapped else 'unrun',physical_device='unrun')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--binary',required=True,type=Path);p.add_argument('--wrapped',type=Path);p.add_argument('--receipt',required=True,type=Path);p.add_argument('--snapshot',type=Path);p.add_argument('--archive',type=Path);a=p.parse_args()
    if a.receipt.exists():p.error('fresh receipt required')
    verify_seal()
    command=None
    if a.snapshot:
        if not a.archive or not a.wrapped:p.error('wrapped source build requires --archive and --wrapped')
        command=build_wrapped(a.snapshot,a.archive,a.wrapped)
    try:result=run(a.binary,a.wrapped)
    except Exception as error:
        a.receipt.write_text(json.dumps(dict(outcome='failed',reason=repr(error),wrapper_build=command),indent=2)+'\n');raise
    result['wrapper_build']=command;a.receipt.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
