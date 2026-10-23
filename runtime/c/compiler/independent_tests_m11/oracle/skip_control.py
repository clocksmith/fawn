"""A missing arrival fails rendezvous even without any memory race to help."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from self_controls import exchange
from phase_controls import ins
from target_m11 import Module
from workgroup_vm import dispatch,Rendezvous


def main():
    p=argparse.ArgumentParser();p.add_argument('--validator',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args();words,ids=exchange();first=next(p for op,args,p in Module(words).ins if op==54);words=words[:first]+ins(54,ids['void'],ids['main'],0,ids['fnt'])+ins(248,ids['entry'])
    for _ in range(3):words+=ins(224,ids['c2'],ids['c2'],ids['sem'])
    words+=ins(253)+ins(56);rows=[]
    with tempfile.TemporaryDirectory(prefix='doe-m11-skip-control-') as temp:
        path=Path(temp)/'barrier-only.spv';path.write_bytes(struct.pack('<'+'I'*len(words),*words));v=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True);assert v.returncode==0,v.stderr
        for q in (1,64):
            for schedule in ['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7']:
                baseline=dispatch(Module(words),{(0,0):bytearray(16)},schedule=schedule,pure_quantum=q)
                try:dispatch(Module(words),{(0,0):bytearray(16)},schedule=schedule,pure_quantum=q,mutation='skip_lane_barrier')
                except Rendezvous as e:rows.append({'quantum':q,'schedule':schedule,'validator_valid':True,'detection':str(e),'baseline':baseline})
                else:raise AssertionError('missing arrival survived')
    a.receipt.write_text(json.dumps({'candidate_used':False,'passed':True,'controls':rows},indent=2)+'\n');print(len(rows),'barrier-only missing-arrival controls passed');return 0
if __name__=='__main__':raise SystemExit(main())
