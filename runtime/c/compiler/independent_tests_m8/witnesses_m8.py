"""Six unchanged M8 anchor sources with separately packed-resource CPU oracles."""
from __future__ import annotations

import hashlib
from pathlib import Path
from exact_numeric_m8 import fp_operation, finite_value, round_bits
from fixtures_m5 import storage, uniform, shape, packed
from fixtures_m6 import trial

U=0xffffffff
S=0xa55a36c9
ONE=0x3f800000
WITNESSES={
 'bench/kernels/package_image_rgba_invert.wgsl':'687a30bc57f40bc94d60e970fc59a140d550890176a91816e1ca1f17a140337a',
 'bench/kernels/matrix_vector_mul_32768x2048_f32.wgsl':'510a86c405550c075ddffe3a58015eab75bead2b724ca75f9b0db58550f968ed',
 'bench/kernels/compilation-corpus/moderate_rope.wgsl':'3ea8c10372222e8836327bd0403b8a9f6f2648e369bce09b51697d122b3123e6',
 'bench/inference-pipeline/kernels/rope.wgsl':'185032b2848ed1e2cb9c2450c0360b77e557cd4f0cb2ec5f5206c37c32471c0e',
 'bench/inference-pipeline/kernels/attention_small_f16kv.wgsl':'808f227fd8fa71f29576998a76c7f81e23d27fe8969e705774be7362bd4f6abc',
 'bench/inference-pipeline/kernels/attention_decode_online_f16kv.wgsl':'0b97b270dfae9e0097e56fb0b7b6f41b228f828c3edb930630649de26a03a990'}


def load(values: list[int], index: int) -> int:
    return values[index] if 0<=index<len(values) else 0


def dot(left: list[int], right: list[int]) -> int:
    acc=0
    for a,b in zip(left,right):
        acc=fp_operation('+',acc,fp_operation('*',a,b))
    return acc


def values(length: int, seed: int=0) -> list[int]:
    fixed=[0x3f800001,0xbf800000,0x40000000,0x3e800000,0xc0400000,0x3f000000,0x40800000]
    return [fixed[(i+seed)%len(fixed)] for i in range(length)]


def metadata(bindings: list, structs: list | None=None) -> dict:
    return dict(entry='main',n=[64,1,1],bindings=bindings,structs=structs or [],builtins=[28],job_only=True)


def image_vectors() -> list:
    result=[]
    pixels=[0,0xffffffff,0x12345678,0x80abcdef,0xff000000,0x00ffffff]
    for count in [0,1,6,9,U]:
        for source_length,dest_length in [(0,0),(0,4),(1,1),(3,7),(6,4),(6,8)]:
            initial={(0,1):pixels[:source_length],(0,2):[S]*dest_length}
            changes={i:load(pixels[:source_length],i)^0x00ffffff for i in range(10) if i<count}
            v=trial(initial,{(0,2):changes},uniforms={(0,0):packed([count,0x5a,0xc3,0xff])})
            v['dispatch']=[{28:(i,0,0)} for i in range(10)]
            result.append(v)
    return result


def matrix_vectors() -> list:
    result=[]
    for alen,blen,outlen in [(0,0,2),(1,1,2),(17,17,3),(2048,2048,3),(4099,2048,4),(2048,17,0)]:
        a,b=values(alen),values(blen,3)
        initial={(0,0):a,(0,1):b,(0,2):[S]*outlen}
        changes={}
        rows=[0,1,2,32767,32768,U]
        for row in rows:
            if row>=32768:
                continue
            changes[row]=dot([load(a,(row*2048+c)&U) for c in range(2048)],
                             [load(b,c) for c in range(2048)])
        v=trial(initial,{(0,2):changes})
        v['dispatch']=[{28:(row,0,0)} for row in rows]
        v['synthetic']=True
        result.append(v)
    return result


def rope_vectors() -> list:
    result=[]
    for seq,dim,heads,offset in [(0,4,2,0),(2,0,1,0),(1,1,1,0),(1,2,1,0),(2,4,2,1),(2,5,2,0),(1,4,1,U)]:
        for truncation in [0,1,1000]:
            size=seq*dim*heads
            q=values(min(size,truncation));freq=values(min((seq+2)*dim,truncation),2)
            half=dim//2
            want=list(q);dispatch=[]
            for z in range(seq+1):
                for y in range(heads+1):
                    for x in range(half+1):
                        dispatch.append({28:(x,y,z)})
                        if x>=half or y>=heads or z>=seq:
                            continue
                        pos=(z+offset)&U
                        base=((z*heads+y)*dim)&U
                        i0=(base+x)&U;i1=(i0+half)&U
                        a,b=load(want,i0),load(want,i1)
                        freq_i=(pos*half+x)&U
                        cosine,sine=load(freq,(freq_i*2)&U),load(freq,(freq_i*2+1)&U)
                        r0=fp_operation('-',fp_operation('*',a,cosine),fp_operation('*',b,sine))
                        r1=fp_operation('+',fp_operation('*',a,sine),fp_operation('*',b,cosine))
                        if i0<len(want):want[i0]=r0
                        if i1<len(want):want[i1]=r1
            v=trial({(0,1):q,(0,2):freq},{},uniforms={(0,0):packed([seq,dim,heads,offset])})
            v['want'][(0,1)]=want;v['dispatch']=dispatch
            v['synthetic']=offset==U
            result.append(v)
    return result


def attention_vectors(decode: bool) -> list:
    result=[]
    for heads,dim,tokens,scale in [(1,0,0,ONE),(1,1,0,ONE),(1,1,1,ONE),(2,2,2,0x3f000000),(1,3,3,0),(2,3,1,0x40000000)]:
        count=heads*dim
        full=(tokens+1)*count if decode else tokens*count
        for truncated in [False,True]:
            length=lambda n:max(0,n-1) if truncated else n
            arrays={i:values(length(count if decode and i<=3 else tokens*count),i)
                    for i in range(1,6 if decode else 4)}
            if not decode:
                arrays={i:values(length(full),i) for i in range(1,4)}
            output=6 if decode else 4
            buffers={(0,i):a for i,a in arrays.items()}
            buffers[(0,output)]=[S]*(length(count if decode else full)+2)
            changes={};dispatch=[]
            for head in range(heads+1):
                for token in range(1 if decode else tokens+1):
                    for dim_i in range(dim+1):
                        dispatch.append({28:(dim_i,head,0) if decode else (dim_i,token,head)})
                        if head>=heads or dim_i>=dim or (not decode and token>=tokens):
                            continue
                        acc=weights=0
                        for past in range(tokens+1 if decode else token+1):
                            head_base=head*dim
                            q_base=head_base if decode else token*count+head_base
                            if decode and past==tokens:
                                k,v=arrays[2],arrays[3];past_base=head_base
                            elif decode:
                                k,v=arrays[4],arrays[5];past_base=past*count+head_base
                            else:
                                k,v=arrays[2],arrays[3];past_base=past*count+head_base
                            score=dot([load(arrays[1],q_base+i) for i in range(dim)],
                                      [load(k,past_base+i) for i in range(dim)])
                            weight=fp_operation('*',score,scale)
                            if finite_value(weight)<0:weight=0
                            acc=fp_operation('+',acc,fp_operation('*',weight,load(v,past_base+dim_i)))
                            weights=fp_operation('+',weights,weight)
                        denom=weights if finite_value(weights)>0 else ONE
                        index=(head*dim+dim_i) if decode else (token*count+head*dim+dim_i)
                        changes[index]=fp_operation('/',acc,denom)
            params=[heads,dim,tokens,scale] if decode else [tokens,dim,heads,scale]
            vector=trial(buffers,{(0,output):changes},uniforms={(0,0):packed(params)})
            vector['dispatch']=dispatch
            result.append(vector)
    return result


def positives(repo: Path) -> list:
    result=[]
    for path,expected_sha in WITNESSES.items():
        source=(repo/path).read_bytes()
        if hashlib.sha256(source).hexdigest()!=expected_sha:
            raise ValueError('Frozen unchanged witness hash mismatch: '+path)
        name=path.replace('/','_').removesuffix('.wgsl')
        if 'image_rgba' in path:
            fields=['pixel_count','_pad0','_pad1','_pad2']
            exp=metadata([uniform('params',0,0,4),storage('input_pixels',1,access=1),storage('output_pixels',2)],
                         [shape('Params',[(f,1) for f in fields])])
            vectors=image_vectors()
        elif 'matrix_vector' in path:
            exp=metadata([storage('matrixData',0,2,1),storage('vectorData',1,2,1),storage('outData',2,2)])
            vectors=matrix_vectors()
        elif 'rope' in path:
            fields=['seq_len','head_dim','num_heads','position_offset']
            exp=metadata([uniform('u',0,0,4),storage('q' if 'moderate' in path else 'qk',1,2),storage('freq_cis',2,2,1)],
                         [shape('Uniforms',[(f,1) for f in fields])])
            vectors=rope_vectors()
        else:
            decode='decode' in path
            fields=['num_heads','head_dim','past_tokens','scale_bits'] if decode else ['seq_len','head_dim','num_heads','scale_bits']
            names=['q','current_k','current_v','past_k','past_v'] if decode else ['q','k','v']
            bindings=[uniform('u',0,0,4)]+[storage(n,i+1,2,1) for i,n in enumerate(names)]+[storage('output',len(names)+1,2)]
            exp=metadata(bindings,[shape('Uniforms',[(f,1) for f in fields])])
            vectors=attention_vectors(decode)
        exp.update(vectors=vectors,source_path=path)
        result.append((name,source,exp))
    return result
