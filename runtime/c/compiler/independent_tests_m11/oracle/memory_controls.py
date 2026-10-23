"""Real SPIR-V vector and StorageBuffer race controls at both quanta."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from self_controls import exchange, mutate
from phase_controls import ins
from target_m11 import Module
from workgroup_vm import dispatch, Race


def vector_components():
    words,ids=exchange()
    words=mutate(words,lambda op,a,p:op==28 and a[0]==ids['arr'],lambda op,a,p:ins(23,ids['arr'],ids['u32'],2))
    words=mutate(words,lambda op,a,p:op==16,lambda op,a,p:ins(16,ids['main'],17,2,1,1))
    return words


def storage_not_fenced():
    words,ids=exchange();rows=[]
    # Reuse a hand-assembled module's valid declarations. Replace body after the
    # prologue barrier with unique lane storage writes, barrier, neighbor reads.
    body=Module(words).functions[ids['main']];prologue=next(p for op,a,p in body if op==224)
    stop=prologue+(words[prologue]>>16)
    words=words[:stop]
    new=words[3];own,plus,neighbor,other,value=range(new,new+5)
    rows+=ins(65,ids['storage_u'],own,ids['out'],ids['c0'],ids['li'])
    rows+=ins(62,own,ids['li'])+ins(224,ids['c2'],ids['c2'],ids['sem'])
    rows+=ins(128,ids['u32'],plus,ids['li'],ids['c1'])+ins(137,ids['u32'],neighbor,plus,ids['c4'])
    rows+=ins(65,ids['storage_u'],other,ids['out'],ids['c0'],neighbor)+ins(61,ids['u32'],value,other)+ins(253)+ins(56)
    words+=rows;words[3]=new+5
    return words


def main():
    p=argparse.ArgumentParser();p.add_argument('--validator',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args();records=[]
    with tempfile.TemporaryDirectory(prefix='doe-m11-memory-controls-') as tmp:
        for name,words in [('vector_components',vector_components()),('storage_not_fenced',storage_not_fenced())]:
            path=Path(tmp)/(name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True)
            assert val.returncode==0,(name,val.stderr)
            for quantum in (1,64):
                for schedule in ['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7']:
                    try:dispatch(Module(words),{(0,0):bytearray(16)},schedule=schedule,pure_quantum=quantum)
                    except Race as error:records.append({'id':name,'quantum':quantum,'schedule':schedule,'validator_valid':True,'detection':str(error)})
                    else:raise AssertionError('memory control survived '+name)
    a.receipt.write_text(json.dumps({'candidate_used':False,'passed':True,'controls':records},indent=2)+'\n')
    print(len(records),'actual target memory controls passed')
    return 0
if __name__=='__main__':raise SystemExit(main())
