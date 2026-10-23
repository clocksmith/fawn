"""Nonzero-poison and wrong-memory-semantics target controls."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from self_controls import exchange,mutate
from phase_controls import ins
from target_m11 import Invalid,Module
from workgroup_vm import dispatch


def zero_module():
    words,ids=exchange();m=Module(words)
    p=next(p for op,a,p in m.ins if op==224);words=words[:p+4]
    n=words[3];ptr,value,outptr=range(n,n+3)
    words+=ins(65,ids['wgscalar'],ptr,ids['shared'],ids['li'])+ins(61,ids['u32'],value,ptr)
    words+=ins(65,ids['storage_u'],outptr,ids['out'],ids['c0'],ids['li'])+ins(62,outptr,value)+ins(253)+ins(56)
    words[3]=n+3
    return words,ids


def main():
    p=argparse.ArgumentParser();p.add_argument('--validator',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args();records=[]
    base,ids=zero_module();missing=mutate(base,lambda op,a,p:op==62 and a[0]==ids['shared'],lambda *a:[])
    wrong=base[:];sid=wrong[3];firstfn=next(p for op,args,p in Module(wrong).ins if op==54)
    wrong[firstfn:firstfn]=ins(43,ids['u32'],sid,0x48);wrong[3]+=1
    wrong=mutate(wrong,lambda op,a,p:op==224,lambda op,a,p:ins(224,a[0],a[1],sid))
    with tempfile.TemporaryDirectory(prefix='doe-m11-zero-controls-') as tmp:
        for name,target in [('zero',base),('missing_zero',missing),('storage_semantics',wrong)]:
            f=Path(tmp)/(name+'.spv');f.write_bytes(struct.pack('<'+'I'*len(target),*target));val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(f)],capture_output=True,text=True)
            assert val.returncode==0,(name,val.stderr)
            for quantum in (1,64):
                for schedule in ['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7']:
                    for fill in (0xa5,0x5a):
                        buffers={(0,0):bytearray([0x77]*16)}
                        try:r=dispatch(Module(target),buffers,schedule=schedule,poison_byte=fill,pure_quantum=quantum)
                        except Invalid as error:
                            assert name=='storage_semantics';detection=str(error)
                        else:
                            if name=='zero':assert buffers[(0,0)]==bytes(16);detection='exact +zero'
                            elif name=='missing_zero':assert buffers[(0,0)]!=bytes(16);detection='nonzero poison visible'
                            else:raise AssertionError('wrong semantics accepted')
                        records.append({'id':name,'quantum':quantum,'schedule':schedule,'poison':fill,'validator_valid':True,'detection':detection})
    a.receipt.write_text(json.dumps({'candidate_used':False,'passed':True,'controls':records},indent=2)+'\n');print(len(records),'zero/semantics cases passed')
    return 0
if __name__=='__main__':raise SystemExit(main())
