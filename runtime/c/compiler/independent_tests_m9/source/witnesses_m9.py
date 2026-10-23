"""Ten unchanged M9 consumer witnesses with independently packed resources.

CPU requirements use source-preserved f32 rounding and independent ideal math
rounded once; this deterministic model is not physical-device qualification.
Each attention summation order has its own result, never an equality waiver.
"""
from __future__ import annotations
import hashlib
from pathlib import Path
from fractions import Fraction
from fixtures_m5 import storage, uniform, shape, packed
from fixtures_m6 import trial
from exact_numeric_m8 import fp_operation as fp, finite_value, round_bits, literal_bits
from interval_math_m9 import reference
from witnesses_m8 import load, values

S=0xa55a36c9
U=0xffffffff
ONE=0x3f800000


def bits(value: str) -> int:return literal_bits(value)
def math(op: str,value: int) -> int:return reference(op,value)['nearest_bits']
def add(a: int,b: int) -> int:return fp('+',a,b)
def mul(a: int,b: int) -> int:return fp('*',a,b)
def div(a: int,b: int) -> int:return fp('/',a,b)
def clamp(value: int,lo: str,hi: str) -> int:
    return bits(lo) if finite_value(value)<finite_value(bits(lo)) else bits(hi) if finite_value(value)>finite_value(bits(hi)) else value


def gelu(x: int) -> int:
    cubic=mul(mul(mul(bits('0.044715'),x),x),x)
    inner=mul(bits('0.7978845608'),add(x,cubic))
    return mul(mul(bits('0.5'),x),add(ONE,math('tanh',clamp(inner,'-15','15'))))


def sequential(items: list[int],initial_zero: bool=True) -> int:
    acc=0 if initial_zero else items[0]
    for x in items if initial_zero else items[1:]:acc=add(acc,x)
    return acc


def pairwise(items: list[int]) -> int:
    assert len(items)==8
    return add(add(add(items[0],items[1]),add(items[2],items[3])),
               add(add(items[4],items[5]),add(items[6],items[7])))


def metadata(bindings: list,structs: list|None=None,size: list|None=None,builtins: list|None=None) -> dict:
    return dict(entry='main',n=size or [256,1,1],bindings=bindings,structs=structs or [],builtins=[28] if builtins is None else builtins,job_only=True)


def positives(repo: Path) -> list:
    cases=[]
    def add_case(path: str,expected: dict,vectors: list) -> None:
        source=(repo/path).read_bytes()
        expected['vectors']=vectors
        expected['source_path']=path
        expected['source_sha256']=hashlib.sha256(source).hexdigest()
        cases.append((Path(path).stem,source,expected))
    config=shape('Config',[(name,1 if i<4 else 2) for i,name in enumerate(['width','height','seed','_pad0','dt','velocity_dissipation','dye_dissipation','force_scale'])])
    expected=metadata([uniform('config',0,0,8),storage('src',1,scalar=2,access=1),storage('dst',2,scalar=2)],[config],[8,8,1])
    vectors=[]
    for width,height in [(0,3),(3,0),(1,1),(3,2),(8,3)]:
        for alen,blen in [(0,0),(0,5),(1,3),(7,5),(24,27)]:
            a=values(alen)
            changes={};dispatch=[]
            for y in range(height+1):
                for x in range(width+1):
                    dispatch.append({28:(x,y,0)})
                    if x<width and y<height:changes[y*width+x]=load(a,y*width+x)
            v=trial({(0,1):a,(0,2):[S]*blen},{(0,2):changes},uniforms={(0,0):packed([width,height,19,3,bits('0.1'),ONE,ONE,bits('4')])})
            v['dispatch']=dispatch;vectors.append(v)
    add_case('bench/kernels/stable_fluids_copy_scalar.wgsl',expected,vectors)
    pattern=[bits(x) for x in ['-20','-4','-1','-0.0','0','0.1','1','4','20']]
    for kind,path in [('moderate','bench/kernels/compilation-corpus/moderate_gelu.wgsl'),('gelu','bench/inference-pipeline/kernels/gelu.wgsl'),('gated','bench/inference-pipeline/kernels/gelu_gated.wgsl'),('silu','bench/kernels/compilation-corpus/simple_silu.wgsl')]:
        if kind=='moderate':
            shape_u=shape('Uniforms',[(x,1) for x in ['size','_pad0','_pad1','_pad2']])
            expected=metadata([uniform('u',0,0,4),storage('input',1,scalar=2,access=1),storage('output',2,scalar=2)],[shape_u])
            akey,okey=(0,1),(0,2)
        elif kind=='gated':
            expected=metadata([storage('gate',0,scalar=2,access=1),storage('up',1,scalar=2,access=1),storage('output',2,scalar=2)])
            akey,okey=(0,0),(0,2)
        else:
            expected=metadata([storage('input',0,scalar=2,access=1),storage('output',1,scalar=2)])
            akey,okey=(0,0),(0,1)
        vectors=[]
        for alen,blen,outlen in [(0,0,0),(0,1,4),(1,0,3),(4,2,9),(9,7,12)]:
            a=pattern[:alen];up=values(blen,2);initial={akey:a,okey:[S]*outlen};changes={}
            if kind=='gated':initial[(0,1)]=up
            for i in range(13):
                if kind=='moderate' and i>=7:continue
                x=load(a,i)
                value=(div(x,add(ONE,math('exp',clamp(x,'-30','30')^0x80000000))) if kind=='silu' else gelu(x))
                if kind=='gated':value=mul(value,load(up,i))
                changes[i]=value
            v=trial(initial,{okey:changes},uniforms={(0,0):packed([7,0,0,0])} if kind=='moderate' else {})
            v['dispatch']=[{28:(i,0,0)} for i in range(13)];vectors.append(v)
        add_case(path,expected,vectors)
    # Different source orders are independently rounded, including cancellation.
    for pair in [False,True]:
        for v2 in [False,True]:
            order='pairwise' if pair else 'forward'
            path=f'bench/inference-pipeline/kernels/attention_slice_{order}{"_v2" if v2 else ""}_f32.wgsl'
            uniforms=shape('Uniforms',[('seq_len',1),('head_dim',1),('scale',2),('_pad0',2)])
            expected=metadata([uniform('params',0,0,4),storage('q',1,scalar=2,access=1),storage('k',2,scalar=2,access=1),storage('v',3,scalar=2,access=1),storage('output',4,scalar=2)],[uniforms],[1,1,1],[])
            vectors=[]
            for variant,qlen,klen,vlen,outlen in [(0,0,0,0,0),(0,0,17,3,2),(0,3,17,16,3),(0,8,64,16,2),(1,8,64,16,2),(2,8,64,16,1)]:
                q=values(qlen,variant);k=values(klen,variant+2);v=values(vlen,variant+4)
                if variant==1:q=[bits(x) for x in ['1e8','1','-1e8','2','0.5','-0.5','1','-1']]
                if variant==2:q=[bits(x) for x in ['2','-2','0.25','1','-1','4','-4','0.5']]
                scale=bits('0.125');scores=[]
                for row in range(8):
                    products=[mul(load(q,i),load(k,row*8+i)) for i in range(8)]
                    score=pairwise(products) if pair else sequential(products,not v2)
                    scores.append(mul(score,scale))
                maximum=max(scores,key=finite_value)
                exps=[math('exp',clamp(fp('-',score,maximum),'-30','30')) for score in scores]
                total=pairwise(exps) if pair else sequential(exps)
                probabilities=[div(e,total) for e in exps]
                answer=[]
                for j in range(2):
                    products=[mul(probabilities[i],load(v,2*i+j)) for i in range(8)]
                    answer.append(pairwise(products) if pair else sequential(products))
                row=trial({(0,1):q,(0,2):k,(0,3):v,(0,4):[S]*outlen},{(0,4):dict(enumerate(answer))},uniforms={(0,0):packed([8,8,scale,0])})
                vectors.append(row)
            add_case(path,expected,vectors)
    uniforms=shape('Uniforms',[('size',1),('eps',2),('_pad0',1),('_pad1',1)])
    expected=metadata([uniform('u',0,0,4),storage('input',1,scalar=2,access=1),storage('weight',2,scalar=2,access=1),storage('output',3,scalar=2)],[uniforms],[1,1,1],[26])
    vectors=[]
    for size in [1,3,8]:
        for alen,wlen,outlen in [(0,0,0),(0,1,5),(2,1,7),(24,8,27)]:
            a=values(alen);weight=values(wlen,1);changes={};eps=bits('0.00001')
            for row in [0,1,2,U]:
                offset=(row*size)&U
                sum_sq=sequential([mul(load(a,(offset+i)&U),load(a,(offset+i)&U)) for i in range(size)])
                rms=div(ONE,math('sqrt',add(div(sum_sq,round_bits(size)),eps)))
                for j in range(size):
                    index=(offset+j)&U
                    changes[index]=mul(mul(load(a,index),rms),load(weight,j))
            row=trial({(0,1):a,(0,2):weight,(0,3):[S]*outlen},{(0,3):changes},uniforms={(0,0):packed([size,eps,0,0])})
            row['dispatch']=[{26:(i,0,0)} for i in [0,1,2,U]];row['synthetic']=True;vectors.append(row)
    add_case('bench/inference-pipeline/kernels/rmsnorm_serial_f32.wgsl',expected,vectors)
    return cases
