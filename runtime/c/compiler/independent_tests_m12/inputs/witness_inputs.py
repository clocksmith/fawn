"""Source-bound whole-output oracle inputs for unchanged M12 compute witnesses.

All numerical references derive from exact arbitrary-integer/rational input data
and outward enclosures, not from candidate targets or compiler intermediates.
"""
from __future__ import annotations
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import struct
from exact_numeric_m8 import finite_value, round_bits
from numeric_oracle import arithmetic, runtime_dot, unary_runtime

REPO=Path('/workspace/shared/doe-c-migration-20261023')
ROOT=Path(__file__).resolve().parent
PATTERN=0xa5a5a5a5


def pack(words):
    return struct.pack('<'+'I'*len(words),*words)


def floats(values):
    return pack([round_bits(x) for x in values])


def interval(value):
    return F(value),F(value)


def f32_literal(value):
    return finite_value(round_bits(F(value)))


def add(a,b):
    return arithmetic('+',a,b)


def mul(a,b):
    return arithmetic('*',a,b)


def sub(a,b):
    return arithmetic('-',a,b)


def div(a,b):
    return arithmetic('/',a,b)


def matrix_data(rows,cols):
    return [[F(((r*13+c*7+c//4)%17)-8,4) for c in range(cols)] for r in range(rows)]


def matrix_case(shared):
    rows=8 if shared else 256
    cols=2048
    matrix=matrix_data(rows,cols)
    vector=[F((c*3+c//5)%13-6,8) for c in range(cols)]
    if shared:
        stored=[matrix[block*4+r][packed*4+lane] for block in range(rows//4) for packed in range(512) for r in range(4) for lane in range(4)]
    else:
        stored=[x for row in matrix for x in row]
    expected=[sum(x*y for x,y in zip(row,vector)) for row in matrix]
    # Every product is dyadic with at most 10 bits and all partial sums are
    # comfortably below 2^24/32. Every source product, lane sum and reduction
    # is exactly representable f32; association/fusion cannot change it.
    assert all(abs(x)<1<<18 for x in expected)
    buffers={(0,0):floats(stored),(0,1):floats(vector),(0,2):pack([PATTERN]*(rows+4))}
    outputs={(0,2):{'exact_words':[round_bits(x) for x in expected]+[PATTERN]*4}}
    path='bench/kernels/matrix_vector_mul_32768x2048_f32_'+('workgroupshared_swizzle1' if shared else 'naive_swizzle0')+'.wgsl'
    return {'id':'matrix_shared' if shared else 'matrix_naive','source':path,'groups':(2,1,1) if shared else (1,1,1),'buffers':buffers,'outputs':outputs,'domain':'Full512 packed columns, all64 invocations in every dispatched workgroup; eight shared or256 naive rows; every input lane nonuniform deterministic dyadic.'}


def gemv_case():
    rows,cols=3,263
    matrix=matrix_data(rows,cols)
    vector=[F((c*5+c//3)%11-5,4) for c in range(cols)]
    expected=[sum(x*y for x,y in zip(row,vector)) for row in matrix]
    buffers={(0,0):pack([rows,cols,0,0]),(0,1):floats([x for row in matrix for x in row]),(0,2):floats(vector),(0,3):pack([PATTERN]*(rows+4))}
    return {'id':'matmul_gemv','source':'bench/inference-pipeline/kernels/matmul-gemv.wgsl','groups':(4,1,1),'buffers':buffers,'outputs':{(0,3):{'exact_words':[round_bits(x) for x in expected]+[PATTERN]*4}},'domain':'Three full active workgroups plus one early-return group;263 columns exercise vec4 stride and scalar tail; all64 lanes and complete shared reduction.'}


def seed_case():
    width,height,seed=5,3,0x93a719b5
    n=width*height
    force=F(1,4)
    config=[width,height,seed,0]+[round_bits(x) for x in [F(1,60),F(1),F(1),force]]
    buffers={(0,0):pack(config),(0,1):pack([PATTERN]*(2*n+4)),(0,2):pack([PATTERN]*(n+4)),(0,3):pack([PATTERN]*(n+4)),(0,4):pack([PATTERN]*(n+4))}
    velocity=[];dye=[]
    for y in range(height):
        for x in range(width):
            centered=[sub(mul(div(interval(F(v)+F(1,2)),interval(d)),interval(2)),interval(1)) for v,d in [(x,width),(y,height)]]
            radius=runtime_dot(centered,centered)
            value=seed^(y*width+x)
            value=(value*747796405+2891336453)&0xffffffff
            value=(((value>>((value>>28)+4))^value)*277803737)&0xffffffff
            mixed=(value>>22)^value
            sample=F(mixed&0xffffff,1<<24)-F(1,2)
            vortex=div(interval(force),add(interval(1),mul(interval(10),radius)))
            sample_term=mul(interval(sample),interval(f32_literal('0.02')))
            negative_y=(-centered[1][1],-centered[1][0])
            velocity.extend([add(mul(negative_y,vortex),sample_term),sub(mul(centered[0],vortex),sample_term)])
            center=sub(interval(1),mul(interval(F(5,2)),radius))
            clamped=(max(F(0),center[0]),max(F(0),center[1]))
            dye.append(add(clamped,mul(interval(sample+F(1,2)),interval(f32_literal('0.05')))))
    assert all(high-low<F(1,10000) for low,high in velocity+dye)
    outputs={(0,1):{'intervals':velocity,'guard_words':4},(0,2):{'intervals':dye,'guard_words':4},(0,3):{'exact_words':[0]*n+[PATTERN]*4},(0,4):{'exact_words':[0]*n+[PATTERN]*4}}
    return {'id':'stable_fluids_seed','source':'bench/kernels/stable_fluids_seed.wgsl','groups':(1,1,1),'buffers':buffers,'outputs':outputs,'domain':'All64 invocations of8x8 tile;15 active cells and49 guarded early returns; independent exact integer scramble and propagated f32 finite intervals.'}


def exp_bounds(a):
    lo=unary_runtime('exp',a[0])[0]
    hi=unary_runtime('exp',a[1])[1]
    return lo,hi


def attention_case():
    heads,dimension,tokens=2,7,5
    q=[[F((h*7+d*3)%9-4,8) for d in range(dimension)] for h in range(heads)]
    k=[[[F((h*5+t*3+d*7)%11-5,8) for d in range(dimension)] for t in range(tokens)] for h in range(heads)]
    v=[[[F((h*11+t*5+d*3)%17-8,4) for d in range(dimension)] for t in range(tokens)] for h in range(heads)]
    scale=F(1,4)
    expected=[]
    for h in range(heads):
        scores=[]
        for t in range(tokens):
            first=runtime_dot([interval(x) for x in q[h][:4]],[interval(x) for x in k[h][t][:4]])
            acc=first
            for d in range(4,dimension):acc=add(acc,mul(interval(q[h][d]),interval(k[h][t][d])))
            scores.append(mul(acc,interval(scale)))
        maximum=(max(x[0] for x in scores),max(x[1] for x in scores))
        exps=[exp_bounds(sub(score,maximum)) for score in scores]
        # The actual64-lane binary reduction adds exact zeros around these five
        # nonzero intervals. Bound every binary tree, preserving finite errors.
        layer=exps+[(F(0),F(0))]*(64-tokens)
        for stride in [32,16,8,4,2,1]:
            layer=[add(layer[i],layer[i+stride]) if i<stride else layer[i] for i in range(len(layer))]
        total=layer[0]
        weights=[div(e,total) for e in exps]
        for d in range(dimension):
            acc=interval(0)
            for t in range(tokens):acc=add(acc,mul(weights[t],interval(v[h][t][d])))
            expected.append(acc)
    assert all(high-low<F(1,10000) for low,high in expected)
    buffers={(0,0):pack([heads,dimension,tokens,round_bits(scale)]),(0,1):floats([x for head in q for x in head]),(0,2):floats([x for head in k for token in head for x in token]),(0,3):floats([x for head in v for token in head for x in token]),(0,4):pack([PATTERN]*(heads*dimension+4))}
    return {'id':'attention_decode','source':'bench/inference-pipeline/kernels/attention-decode.wgsl','groups':(3,1,1),'buffers':buffers,'outputs':{(0,4):{'intervals':expected,'guard_words':4}},'domain':'Two complete active64-lane workgroups and one early-return group;dim7 vector+scalar tail, five nonuniform logits, complete max/sum/softmax/reduction and every output including odd last dimension.'}


def cases():
    return [seed_case(),matrix_case(False),matrix_case(True),gemv_case(),attention_case()]


def freeze():
    rows=[]
    for case in cases():
        raw=(REPO/case['source']).read_bytes()
        row={key:value for key,value in case.items() if key not in {'buffers','outputs'}}
        row['source_sha256']=hashlib.sha256(raw).hexdigest()
        row['buffers']={str(key):{'bytes':len(value),'sha256':hashlib.sha256(value).hexdigest()} for key,value in case['buffers'].items()}
        row['outputs']={str(key):{k:[[str(x),str(y)] for x,y in value] if k=='intervals' else value for k,value in output.items()} for key,output in case['outputs'].items()}
        rows.append(row)
    (ROOT/'WITNESS_CASES.json').write_text(json.dumps({'candidate_observed':False,'additive_pre_candidate_observation':True,'rows':rows},indent=2,sort_keys=True)+'\n')
    print(json.dumps({'unchanged_witnesses':len(rows),'all_source_hashes':[x['source_sha256'] for x in rows],'candidate_observed':False}))

if __name__=='__main__':freeze()
