#!/usr/bin/env python3
"""Independent oracle sensitivity controls, no candidate compiler required."""
from __future__ import annotations
import argparse,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
from fractions import Fraction
from model_m10 import Schema,resource,raw_words
from exact_numeric_m8 import self_test,round_bits
from target_m10 import Module,Aggregate,Invalid
from run_m10 import cases,expected_buffers

def ins(op,*a):return [((len(a)+1)<<16)|op,*a]
def string(s):
    b=s.encode()+b'\0';b+=b'\0'*((-len(b))%4);return list(struct.unpack('<'+'I'*(len(b)//4),b))
def module(mode='shuffle',stride=16,offset=0,swap=False):
    # Hand-authored SPIR-V 1.3, not compiler output. Type and IDs are fixed.
    w=[0x07230203,0x00010300,0,90,0]
    rows=[ins(17,1),ins(14,0,1),ins(15,5,40,*string('main')),ins(16,40,17,1,1,1),
          ins(71,4,6,stride),ins(71,5,2),ins(72,5,0,35,offset),ins(71,10,34,0),ins(71,10,33,1),ins(71,11,34,0),ins(71,11,33,0),
          ins(19,1),ins(21,2,32,0),ins(23,3,2,3),ins(29,4,3),ins(30,5,4),ins(32,6,12,5),ins(32,7,12,3),ins(33,8,1),
          ins(43,2,9,0),ins(43,2,12,1),ins(43,2,13,2),ins(43,2,14,5),ins(43,2,15,11),ins(43,2,16,99),
          ins(44,3,17,13,14,15),ins(32,18,7,3),ins(59,6,10,12),ins(59,6,11,12),ins(54,1,40,0,8),ins(248,41)]
    if mode=='shuffle':rows += [ins(65,7,42,10,9,12),ins(61,3,43,42),ins(79,3,44,43,43,*( (0,1,2) if swap else (2,1,0)))]
    elif mode=='snapshot':rows += [ins(59,18,20,7,17),ins(61,3,43,20),ins(82,3,44,16,43,0),ins(62,20,44),ins(83,3,44 if False else 47,43)]
    else:raise ValueError(mode)
    rows += [ins(65,7,45,11,9,9),ins(62,45,44 if mode=='shuffle' else 43),ins(253),ins(56)]
    for row in rows:w+=row
    return w

def guarded_module(wrong_binding=False):
    w=module();m=Module(w);cut=next(p for op,a,p in m.ins if op==54)
    w=w[:cut]+ins(20,21)+ins(46,3,22)
    rows=[ins(54,1,40,0,8),ins(248,41),ins(68,2,42,11 if wrong_binding else 10,0),
          ins(176,21,43,12,42),ins(247,48,0),ins(250,43,44,47),ins(248,44),
          ins(65,7,45,10,9,12),ins(61,3,46,45),ins(249,48),ins(248,47),ins(249,48),
          ins(248,48),ins(245,3,49,46,44,22,47),ins(65,7,50,11,9,9),ins(62,50,49),ins(253),ins(56)]
    for row in rows:w+=row
    return w

def typed_arithmetic_module(as_float=False):
    # Index type2 remains integer; value type19 alone changes numeric family.
    w=module();m=Module(w);rows=[]
    for op,a,p in m.ins:
        if op==54:break
        if op==23:a=(a[0],19,a[2])
        if op==43 and a[1] in {13,14,15,16}:a=(19,*a[1:])
        if op==23:rows.append(ins(22,19,32) if as_float else ins(21,19,32,1))
        rows.append(ins(op,*a))
    w=w[:5]
    for row in rows:w+=row
    for row in [ins(54,1,40,0,8),ins(248,41),ins(65,7,42,10,9,9),ins(61,3,43,42),ins(129 if as_float else 128,3,44,43,17),ins(65,7,45,11,9,9),ins(62,45,44),ins(253),ins(56)]:w+=row
    return w

def validate(words,validator):
    if not validator:return 'unrun'
    with tempfile.TemporaryDirectory(prefix='doe-m10-self-',dir='/tmp') as tmp:
        p=Path(tmp)/'control.spv';p.write_bytes(struct.pack('<'+'I'*len(words),*words))
        r=subprocess.run([validator,'--target-env','vulkan1.1',str(p)],capture_output=True,text=True)
        if r.returncode:raise AssertionError(r.stderr)
    return 'passed'

def run(validator=None):
    checks=[];self_test();checks.append('exact rational scalar known vectors')
    sc=Schema();v=sc.vec(3,'f32');a=sc.array(v,2);s=sc.structure('S',[('v',v),('x','f32')]);inn=sc.structure('Inner',[('x','f32')]);outer=sc.structure('Outer',[('a',inn),('b','f32')]);padded=sc.structure('Padded',[('a',inn,16,16),('b','f32')]);tail=sc.structure('Tail',[('h',v),('tail',sc.array('f32',None))]);t32=sc.structure('Tail32',[('h','u32',32),('tail',sc.array(v,None))]);l=sc.layouts()
    assert (l[s]['size'],l[s]['align'],[m['offset'] for m in l[s]['members']])==(16,16,[0,12])
    assert (l[a]['stride'],l[a]['size'])==(16,32)
    assert (l[inn]['size'],l[outer]['size'],[m['offset'] for m in l[outer]['members']])==(4,8,[0,4])
    assert (l[padded]['size'],[m['offset'] for m in l[padded]['members']])==(32,[0,16])
    assert (l[tail]['tail'][1:],sc.minimum(tail))==((12,4),16)
    assert (l[t32]['tail'][1:],sc.minimum(t32))==((16,16),32)
    checks.append('six literal WGSL layout answers and tail minimum distinction')
    data=raw_words([101,102,103,0xcccccccc,11,23,37,0xdddddddd,53,71,97,0xeeeeeeee,131,151,173,0xffffffff])
    sentinel=bytearray(raw_words([0xA1B2C3D4]*16));expected=bytearray(sentinel);expected[:12]=raw_words([37,23,11]);mutants=[]
    for name,words in [('baseline',module()),('shuffle_lane',module(swap=True)),('stride',module(stride=32)),('member_offset',module(offset=16))]:
        validity=validate(words,validator);buffers={(0,0):bytearray(sentinel),(0,1):bytearray(data)};m=Module(words)
        try:m.execute(buffers,readonly={(0,1)});outcome='matches' if buffers[(0,0)]==expected else 'differs'
        except Invalid:outcome='unsafe'
        if name=='baseline':assert outcome=='matches'
        else:assert outcome!='matches';mutants.append(dict(name=name,validator=validity,kill=outcome))
    checks.append('validator-valid lane/stride/member-offset mutants killed by independent bytes')
    for wrong in (False,True):
        words=guarded_module(wrong);validity=validate(words,validator);m=Module(words)
        buffers={(0,0):bytearray(sentinel),(0,1):bytearray(raw_words([3,5,7,0]))}
        try:m.execute(buffers,readonly={(0,1)});outcome='matches' if buffers[(0,0)][:12]==bytes(12) else 'differs'
        except Invalid:outcome='unsafe'
        if not wrong:assert outcome=='matches'
        else:assert outcome=='unsafe';mutants.append(dict(name='guard_other_binding',validator=validity,kill=outcome))
    wanted=raw_words([0x3f800002,0x40000005,0x4040000b])
    for wrong in (False,True):
        words=typed_arithmetic_module(wrong);validity=validate(words,validator);m=Module(words)
        buffers={(0,0):bytearray(sentinel),(0,1):bytearray(raw_words([0x3f800000,0x40000000,0x40400000,0]))}
        m.execute(buffers,readonly={(0,1)});matches=buffers[(0,0)][:12]==wanted
        if not wrong:assert matches
        else:assert not matches;mutants.append(dict(name='value_type_family',validator=validity,kill='differs'))
    checks.append('validator-valid wrong-binding guard and wrong value-TypeId family mutants killed')

    words=module(mode='snapshot');validate(words,validator);m=Module(words);buffers={(0,0):bytearray(sentinel),(0,1):bytearray(data)};m.execute(buffers)
    assert buffers[(0,0)][:12]==raw_words([2,5,11]);checks.append('immutable by-value local snapshot')
    m.types[80]=(28,(2,81));m.constants[81]=1000000000
    huge=m.zero(80);changed=m.insert(huge,(999999999,),71)
    assert isinstance(huge,Aggregate) and huge.children is None and len(changed.changes)==1
    assert m.extract(huge,(999999999,))==0 and m.extract(changed,(999999999,))==71 and m.extract(changed,(0,))==0
    checks.append('billion-element typed zero uses one sparse update, not flattening')
    for op,args in [(135,[0x80000000,0xffffffff]),(134,[7,0]),(138,[7,0]),(196,[1,32]),(109,[0x7fc12345]),(110,[0x7f800000]),(109,[0xbf800000])]:
        try:m.scalar(op,args)
        except Invalid:pass
        else:raise AssertionError(('unsafe numeric accepted',op,args))
    checks.append('unsafe numeric instructions rejected before any later select')
    assert m.scalar(180,[0x80000000,0]) is True
    assert m.scalar(180,[0x7fc12345,0x7fc12345]) is False
    assert m.scalar(183,[0x7fc12345,0]) is True
    assert m.scalar(195,[0x80000000,31])==0xffffffff
    checks.append('IEEE signed zero, NaN ordered/unordered and signed-shift controls')
    bad=module();bad[3]=40
    try:Module(bad)
    except Invalid:pass
    else:raise AssertionError('bad ID bound accepted')
    checks.append('malformed target bound rejected')
    fixtures=cases();names=[c['name'] for c in fixtures];assert len(names)==len(set(names))
    real=[c for c in fixtures if 'real_witness' in c['tags']]
    assert len(real)==18 and len({c['source'] for c in real})==3
    assert {c['name'].split('_')[-1] for c in real}=={'full','short'}
    checks.append('all three real witnesses, three grid shapes, both range variants present')
    for c in fixtures:
        if not c['status']:
            assert c['reflection']==c['schema'].reflection(c['resources'],local=c['local'])
            for key,data in expected_buffers(c).items():assert len(data)==len(c['buffers'][key])
    checks.append('deterministic source-schema inventory and finite expected byte spans')
    return dict(format='m10-oracle-self-controls-v1',candidate_used=False,case_count=len(fixtures),checks=checks,mutants=mutants,validator=validator or 'unrun',physical_device='unrun')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--validator');p.add_argument('--receipt');a=p.parse_args();report=run(a.validator);text=json.dumps(report,indent=2,sort_keys=True)+'\n'
    if a.receipt:Path(a.receipt).write_text(text)
    else:print(text)
