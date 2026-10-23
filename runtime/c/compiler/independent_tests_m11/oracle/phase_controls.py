"""Validator-valid actual callsite/loop mismatches and race granularity controls."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from self_controls import exchange, mutate
from target_m11 import Invalid, Module, Pointer
from workgroup_vm import MemoryObserver, Race, Rendezvous, dispatch


def ins(op,*args):return [(len(args)+1)<<16|op,*args]


def callsite_mismatch():
    words,ids=exchange();m=Module(words);next_id=words[3]
    names=['helper','left','right','join','calla','callb','helperlabel']
    fresh={n:next_id+i for i,n in enumerate(names)}
    barrier=[p for op,a,p in m.ins if op==224][1]
    rows=(ins(247,fresh['join'],0)+ins(250,ids['iszero'],fresh['left'],fresh['right'])+
          ins(248,fresh['left'])+ins(57,ids['void'],fresh['calla'],fresh['helper'])+
          ins(249,fresh['join'])+ins(248,fresh['right'])+
          ins(57,ids['void'],fresh['callb'],fresh['helper'])+ins(249,fresh['join'])+ins(248,fresh['join']))
    out=mutate(words,lambda op,a,p:p==barrier,lambda *a:rows)
    out+=ins(54,ids['void'],fresh['helper'],0,ids['fnt'])+ins(248,fresh['helperlabel'])+ins(224,ids['c2'],ids['c2'],ids['sem'])+ins(253)+ins(56)
    out[3]=next_id+len(names)
    return out


def loop_phase_mismatch():
    words,ids=exchange();m=Module(words);next_id=words[3]
    names=['head','body','continuing','done','iteration','next','test','skip','phasezero','join','barrierblock','condition']
    f={n:next_id+i for i,n in enumerate(names)}
    first_barrier=[p for op,a,p in m.ins if op==224][0]
    # A loop after prologue. Lane0 skips barrier in iteration0, then meets the
    # others' iteration0 at the identical static barrier while it is in iteration1.
    rows=(ins(249,f['head'])+ins(248,f['head'])+
          ins(245,ids['u32'],f['iteration'],ids['c0'],ids['merge'],f['next'],f['continuing'])+
          ins(176,ids['bool'],f['test'],f['iteration'],ids['c2'])+
          ins(246,f['done'],f['continuing'],0)+ins(250,f['test'],f['body'],f['done'])+
          ins(248,f['body'])+ins(170,ids['bool'],f['phasezero'],f['iteration'],ids['c0'])+
          ins(167,ids['bool'],f['condition'],ids['iszero'],f['phasezero'])+
          ins(247,f['join'],0)+ins(250,f['condition'],f['join'],f['barrierblock'])+
          ins(248,f['barrierblock'])+ins(224,ids['c2'],ids['c2'],ids['sem'])+ins(249,f['join'])+
          ins(248,f['join'])+ins(249,f['continuing'])+ins(248,f['continuing'])+
          ins(128,ids['u32'],f['next'],f['iteration'],ids['c1'])+ins(249,f['head'])+ins(248,f['done']))
    out=mutate(words,lambda op,a,p:p==first_barrier,lambda op,a,p:ins(op,*a)+rows)
    out[3]=next_id+len(names)
    return out


def main():
    p=argparse.ArgumentParser();p.add_argument('--validator',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args();records=[]
    with tempfile.TemporaryDirectory(prefix='doe-m11-real-phase-controls-') as tmp:
        for name,words in [('actual_helper_callsite',callsite_mismatch()),('actual_loop_iteration',loop_phase_mismatch())]:
            f=Path(tmp)/(name+'.spv');f.write_bytes(struct.pack('<'+'I'*len(words),*words))
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(f)],capture_output=True,text=True)
            assert val.returncode==0,(name,val.stderr)
            try:dispatch(Module(words),{(0,0):bytearray(16)},(1,1,1))
            except Rendezvous as error:records.append({'name':name,'validator_valid':True,'caught':str(error)})
            else:raise AssertionError('actual phase mismatch survived '+name)
    words,ids=exchange();m=Module(words);epochs={(0,0,0):0}
    # Race granularity self-control uses actual target vector type and pointers.
    m.types[200]=(23,(ids['u32'],2));m.types[201]=(32,(4,200));m.variables[202]=(201,4,None)
    observer=MemoryObserver(m,{(0,0):bytearray(16)},epochs)
    observer.access((0,(0,0,0)),Pointer(202,(0,)),True)
    try:observer.access((1,(0,0,0)),Pointer(202,(1,)),True)
    except Race as error:records.append({'name':'same_vector_components','caught':str(error)})
    else:raise AssertionError('same vector race survived')
    observer=MemoryObserver(m,{(0,0):bytearray(16)},epochs)
    observer.access((0,(0,0,0)),Pointer(ids['out'],(0,0)),True)
    epochs[(0,0,0)]+=1
    try:observer.access((1,(0,0,0)),Pointer(ids['out'],(0,0)),False)
    except Race as error:records.append({'name':'storage_not_workgroup_fenced','caught':str(error)})
    else:raise AssertionError('storage fence race survived')
    a.receipt.write_text(json.dumps({'candidate_used':False,'passed':True,'controls':records},indent=2)+'\n')
    print(a.receipt.read_text())
    return 0
if __name__=='__main__':raise SystemExit(main())
