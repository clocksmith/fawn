"""Hand-built SPIR-V numeric controls, independent of every compiler candidate."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from exact_numeric_m8 import round_bits
from target_m12 import Invalid, Module
from workgroup_m12 import dispatch


class Builder:
    def __init__(self):
        self.ids={};self.next=1;self.rows=[]
    def ident(self,name):
        if name not in self.ids:
            self.ids[name]=self.next;self.next+=1
        return self.ids[name]
    def emit(self,opcode,*args):
        self.rows.extend([(len(args)+1)<<16|opcode,*args])
    def string(self,value):
        raw=value.encode()+b'\0';raw+=b'\0'*(-len(raw)%4)
        return struct.unpack('<'+'I'*(len(raw)//4),raw)
    def words(self):
        return [0x07230203,0x00010300,0,self.next,0]+self.rows


def shader(opcode,arguments,mutation=''):
    b=Builder();i=b.ident;e=b.emit
    width=len(arguments[0]);result_width=1 if opcode in {148,66,67} else width
    for name in ['void','u32','f32','v2','v3','v4','fn','import','main','entry','ra','block','sptr','uptr','output']:
        i(name)
    e(17,1)
    e(11,i('import'),*b.string('GLSL.std.450'))
    e(14,0,1);e(15,5,i('main'),*b.string('main'));e(16,i('main'),17,1,1,1)
    e(71,i('ra'),6,4);e(71,i('block'),2);e(72,i('block'),0,35,0)
    e(71,i('output'),34,0);e(71,i('output'),33,0)
    e(19,i('void'));e(21,i('u32'),32,0);e(22,i('f32'),32)
    for n in [2,3,4]:e(23,i('v'+str(n)),i('f32'),n)
    e(33,i('fn'),i('void'))
    for n in range(5):e(43,i('u32'),i('c'+str(n)),n)
    for ai,arg in enumerate(arguments):
        for lane,value in enumerate(arg):e(43,i('f32'),i(f'arg{ai}_{lane}'),round_bits(value))
        if width>1:e(44,i('v'+str(width)),i('arg'+str(ai)),*[i(f'arg{ai}_{lane}') for lane in range(width)])
    e(29,i('ra'),i('u32'));e(30,i('block'),i('ra'));e(32,i('sptr'),12,i('block'));e(32,i('uptr'),12,i('u32'));e(59,i('sptr'),i('output'),12)
    e(54,i('void'),i('main'),0,i('fn'));e(248,i('entry'))
    argument_ids=[i('arg'+str(ai)) if width>1 else i(f'arg{ai}_0') for ai in range(len(arguments))]
    if mutation=='swap_cross':argument_ids.reverse()
    result_type=i('f32') if result_width==1 else i('v'+str(result_width))
    if mutation=='wrong_return':result_type=i('v2') if result_width==1 else i('f32')
    if opcode==148:e(148,result_type,i('answer'),*argument_ids)
    else:e(12,result_type,i('answer'),i('import'),opcode,*argument_ids)
    for lane in range(result_width):
        value=i('answer')
        if result_width>1:e(81,i('f32'),i('extract'+str(lane)),value,lane);value=i('extract'+str(lane))
        e(124,i('u32'),i('encoded'+str(lane)),value)
        e(65,i('uptr'),i('address'+str(lane)),i('output'),i('c0'),i('c'+str(lane)))
        e(62,i('address'+str(lane)),i('encoded'+str(lane)))
    e(253);e(56)
    words=b.words()
    if mutation=='id_undercount':words[3]-=1
    if mutation=='word_undercount':words.pop()
    return words


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(exist_ok=False)
    controls=[('dot',148,[[1,2,3],[4,5,6]],[round_bits(32)]),('length',66,[[3,4]],[round_bits(5)]),('distance',67,[[-1,2],[2,6]],[round_bits(5)]),('cross',68,[[1,2,3],[4,5,6]],[round_bits(-3),round_bits(6),round_bits(-3)]),('sqrt',31,[[1,4,9,16]],[round_bits(x) for x in [1,2,3,4]]),('normalize',69,[[0,0,-7]],[0,0,round_bits(-1)])]
    rows=[]
    for name,op,args,expected in controls:
        for mutation in ['', 'wrong_return','id_undercount','word_undercount']+(['swap_cross'] if op==68 else []):
            words=shader(op,args,mutation);path=a.out/(name+'_'+(mutation or 'base')+'.spv');path.write_bytes(struct.pack('<'+'I'*len(words),*words))
            val=subprocess.run([str(a.validator),'--target-env','vulkan1.1',str(path)],capture_output=True,text=True)
            row={'id':name+'_'+(mutation or 'base'),'validator_status':val.returncode,'validator_output':val.stdout+val.stderr,'target_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
            if not mutation or mutation=='swap_cross':assert val.returncode==0,row
            else:assert val.returncode!=0,row
            try:
                buffers={(0,0):bytearray(b'\xa5'*32)}
                result=dispatch(Module(words),buffers,(1,1,1),'round_robin',pure_quantum=1)
                actual=list(struct.unpack('<8I',buffers[(0,0)]))
                survived=actual[:len(expected)]==expected and actual[len(expected):]==[0xa5a5a5a5]*(8-len(expected))
                if mutation and survived and val.returncode==0:raise AssertionError('mutation survived validator and model')
                if not mutation and not survived:raise AssertionError(('base mismatch',actual,expected))
                row.update(actual=actual,detection=('validator rejection' if survived else 'exact output mismatch') if mutation else 'base correct',execution=result)
            except Invalid as error:
                if not mutation:raise
                row['detection']=str(error)
            rows.append(row)
    report={'candidate_used':False,'passed':True,'controls':rows,'validator_sha256':hashlib.sha256(a.validator.read_bytes()).hexdigest()}
    (a.out/'results.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    print(json.dumps({'passed':True,'controls':len(rows),'candidate_used':False}))

if __name__=='__main__':main()
