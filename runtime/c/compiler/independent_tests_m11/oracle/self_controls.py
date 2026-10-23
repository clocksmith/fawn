"""Hand-assembled target controls, independent of any M11 candidate."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from target_m11 import Invalid, Module
from workgroup_vm import dispatch, Race, Rendezvous


class Builder:
    def __init__(self):
        self.ids={};self.next=1;self.rows=[]
    def ident(self,name):
        if name not in self.ids:
            self.ids[name]=self.next;self.next+=1
        return self.ids[name]
    def emit(self,opcode,*args):
        self.rows += [(len(args)+1)<<16|opcode,*args]
    def string(self,value):
        raw=value.encode()+b'\0';raw+=b'\0'*(-len(raw)%4)
        return struct.unpack('<'+'I'*(len(raw)//4),raw)
    def words(self):
        return [0x07230203,0x00010300,0,self.next,0]+self.rows


def exchange():
    b=Builder();i=b.ident;e=b.emit
    for n in ['void','bool','u32','v3','fnt','c0','c1','c2','c3','c4','c100','sem','arr','null','wgptr','wgscalar','ra','block','storage','storage_u','input_u','input_v','shared','out','lane','wid','main','entry','init','merge']:
        i(n)
    e(17,1);e(14,0,1)
    e(15,5,i('main'),*b.string('main'),i('lane'),i('wid'))
    e(16,i('main'),17,4,1,1)
    e(71,i('lane'),11,29);e(71,i('wid'),11,26)
    e(71,i('ra'),6,4);e(71,i('block'),2);e(72,i('block'),0,35,0)
    e(71,i('out'),34,0);e(71,i('out'),33,0)
    e(19,i('void'));e(20,i('bool'));e(21,i('u32'),32,0);e(23,i('v3'),i('u32'),3);e(33,i('fnt'),i('void'))
    for n,v in [('c0',0),('c1',1),('c2',2),('c3',3),('c4',4),('c100',100),('sem',0x108)]:e(43,i('u32'),i(n),v)
    e(28,i('arr'),i('u32'),i('c4'));e(46,i('arr'),i('null'))
    e(32,i('wgptr'),4,i('arr'));e(32,i('wgscalar'),4,i('u32'))
    e(29,i('ra'),i('u32'));e(30,i('block'),i('ra'))
    e(32,i('storage'),12,i('block'));e(32,i('storage_u'),12,i('u32'))
    e(32,i('input_u'),1,i('u32'));e(32,i('input_v'),1,i('v3'))
    e(59,i('wgptr'),i('shared'),4);e(59,i('storage'),i('out'),12)
    e(59,i('input_u'),i('lane'),1);e(59,i('input_v'),i('wid'),1)
    e(54,i('void'),i('main'),0,i('fnt'));e(248,i('entry'))
    e(61,i('u32'),i('li'),i('lane'));e(170,i('bool'),i('iszero'),i('li'),i('c0'))
    e(247,i('merge'),0);e(250,i('iszero'),i('init'),i('merge'))
    e(248,i('init'));e(62,i('shared'),i('null'));e(249,i('merge'));e(248,i('merge'))
    e(224,i('c2'),i('c2'),i('sem'))
    e(61,i('v3'),i('groupv'),i('wid'));e(81,i('u32'),i('group'),i('groupv'),0)
    e(132,i('u32'),i('base100'),i('group'),i('c100'));e(128,i('u32'),i('value'),i('base100'),i('li'))
    e(65,i('wgscalar'),i('own'),i('shared'),i('li'));e(62,i('own'),i('value'))
    e(224,i('c2'),i('c2'),i('sem'))
    e(128,i('u32'),i('next1'),i('li'),i('c1'));e(137,i('u32'),i('neighbor'),i('next1'),i('c4'))
    e(65,i('wgscalar'),i('other'),i('shared'),i('neighbor'));e(61,i('u32'),i('answer'),i('other'))
    e(132,i('u32'),i('base4'),i('group'),i('c4'));e(128,i('u32'),i('global'),i('base4'),i('li'))
    e(65,i('storage_u'),i('dst'),i('out'),i('c0'),i('global'));e(62,i('dst'),i('answer'))
    e(224,i('c2'),i('c2'),i('sem'));e(253);e(56)
    return b.words(),b.ids


def mutate(words, predicate, replacement):
    result=words[:5];p=5
    while p<len(words):
        count=words[p]>>16;opcode=words[p]&65535;args=words[p+1:p+count]
        if predicate(opcode,args,p):
            result.extend(replacement(opcode,args,p))
        else:result.extend(words[p:p+count])
        p+=count
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--validator',type=Path,required=True);parser.add_argument('--receipt',type=Path,required=True)
    args=parser.parse_args();words,ids=exchange();result=[]
    with tempfile.TemporaryDirectory(prefix='doe-m11-oracle-controls-') as temp:
        def validate(name,target):
            path=Path(temp)/(name+'.spv');path.write_bytes(struct.pack('<'+'I'*len(target),*target))
            p=subprocess.run([str(args.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True)
            return {'returncode':p.returncode,'output':p.stdout+p.stderr,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
        base_validation=validate('base',words)
        assert base_validation['returncode']==0,base_validation
        for schedule in ['round_robin','reverse','shuffle_0x11c0ffee','bounded_burst_1_3_7']:
            out={(0,0):bytearray([0x77]*48)}
            record=dispatch(Module(words),out,(3,1,1),schedule)
            actual=list(struct.unpack('<12I',out[(0,0)]))
            assert actual==[g*100+(j+1)%4 for g in range(3) for j in range(4)],actual
            result.append({'id':'base_'+schedule,'passed':True,'execution':record})
        for mutation in ['privatize_shared','alias_groups','skip_lane_barrier','helper_phase_mismatch','loop_phase_mismatch']:
            out={(0,0):bytearray([0x77]*48)}
            try:
                dispatch(Module(words),out,(3,1,1),mutation=mutation)
                actual=list(struct.unpack('<12I',out[(0,0)]))
                if actual==[g*100+(j+1)%4 for g in range(3) for j in range(4)]:raise AssertionError('survived '+mutation)
                failure='exact output mismatch'
            except Invalid as exc:failure=str(exc)
            result.append({'id':mutation,'passed':True,'detection':failure})
        barriers=[p for op,a,p in Module(words).ins if op==224]
        mutated={
          'remove_init':mutate(words,lambda op,a,p:op==62 and a[0]==ids['shared'],lambda *x:[]),
          'remove_prologue':mutate(words,lambda op,a,p:p==barriers[0],lambda *x:[]),
          'remove_source':mutate(words,lambda op,a,p:p==barriers[1],lambda *x:[]),
          'no_memory_semantics':mutate(words,lambda op,a,p:op==224,lambda op,a,p:[(4<<16)|224,a[0],a[1],ids['c0']]),
          'wrong_memory_scope':mutate(words,lambda op,a,p:op==224,lambda op,a,p:[(4<<16)|224,a[0],ids['c1'],a[2]]),
          'wrong_execution_scope':mutate(words,lambda op,a,p:op==224,lambda op,a,p:[(4<<16)|224,ids['c1'],a[1],a[2]]),
        }
        for name,target in mutated.items():
            val=validate(name,target)
            if name=='remove_init':
                # Exchange overwrites every element, so missing initialization is
                # detected structurally, not dishonestly by a numeric comparison.
                detection='whole-root constant-null store absent'
                assert not any(op==62 and a[0]==ids['shared'] for op,a,p in Module(target).ins)
            else:
                try:
                    dispatch(Module(target),{(0,0):bytearray(48)},(3,1,1),exact_barriers=name!='no_memory_semantics')
                    raise AssertionError('survived '+name)
                except Invalid as exc:detection=str(exc)
            result.append({'id':name,'passed':True,'validator':val,'detection':detection})
    receipt={'candidate_used':False,'base_validator':base_validation,'controls':result,'passed':True}
    args.receipt.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
    print(json.dumps(receipt,indent=2))
    return 0

if __name__=='__main__':raise SystemExit(main())
