"""Add exact AF envelope predicates and vector finite-edge controls before observation."""
from __future__ import annotations
from datetime import datetime,timezone
from fractions import Fraction
import hashlib,json
from pathlib import Path
from exact_numeric_m8 import finite_value,round_bits
from interval_math_m9 import reference,ordered
from numeric_oracle import float_graph

R=Path(__file__).resolve().parent
D=R/'supplement_numeric';D.mkdir(exist_ok=False)
OUT='@group(0) @binding(1) var<storage,read_write> output:array<u32>;'
ENTRY='@compute @workgroup_size(1) fn main()'
rows=[]


def hex64(bits):
    sign='-' if bits>>63 else ''
    exp=(bits>>52)&2047;mant=bits&((1<<52)-1)
    if exp==0:return sign+'0x0.'+format(mant,'013x')+'p-1022'
    assert exp!=2047
    return sign+'0x1.'+format(mant,'013x')+'p'+str(exp-1023)


def save(name,source,status=0,**kw):
    path=D/(name+'.wgsl');path.write_text(source+'\n')
    rows.append(dict(id=name,source=str(path.relative_to(R)),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),expected_status=status,**kw))


for kind in ['exp','tanh','sqrt','dot','length','distance','normalize','cross']:
    widths=[3] if kind=='cross' else [2,3,4]
    for width in widths:
        args=([[-2,-1,0,2]] if kind in {'exp','tanh'} else [[1,2,3,7]] if kind in {'sqrt','length','normalize'} else [[1,2,3,7],[4,5,6,-2]])
        args=[x[:width] for x in args]
        bits=[[round_bits(Fraction(x),64) for x in row] for row in args]
        allowed=float_graph(kind,bits,64)
        expression=kind+'('+','.join(f'vec{width}('+','.join(str(x)+'.0' for x in row)+')' for row in args)+')'
        conditions=[]
        for lane,options in enumerate(allowed):
            value='result' if kind in {'dot','length','distance'} else 'result.'+'xyzw'[lane]
            exact=sorted(options,key=lambda x:ordered(x,64))
            if len(exact)==1:conditions.append(value+'=='+hex64(exact[0]))
            else:conditions.append('('+value+'>='+hex64(exact[0])+' && '+value+'<='+hex64(exact[-1])+')')
        source=OUT+'const result='+expression+';'+ENTRY+'{if('+ ' && '.join(conditions)+'){output[0]=1u;}else{output[0]=2u;}}'
        save(f'af_exact_envelope_{kind}_{width}',source,expected_options=[[1]],output_words=8,obligation='Observe binary64 result before f32 materialization; chosen host envelope, not normative AF ULP mandate')

edge=[
 ('vector_signed_zero_dot','dot(vec2f(-0.0f,-0.0f),vec2f(1.0f,1.0f))',[0x80000000]),
 ('vector_signed_zero_min','min(vec2f(0.0f,-0.0f),vec2f(-0.0f,0.0f))',[0x80000000,0]),
 ('vector_signed_zero_max','max(vec2f(0.0f,-0.0f),vec2f(-0.0f,0.0f))',[0x80000000,0]),
 ('vector_subnormal_dot','dot(vec2f(0x1p-149f,0.0f),vec2f(1.0f,1.0f))',[1]),
 ('vector_finite_underflow_exp','exp(vec2f(-110.0f,-120.0f))',[0,0]),
 ('af_rounded_graph_cancellation','dot(vec3(9007199254740992.0,1.0,-9007199254740992.0),vec3(1.0,1.0,1.0))',[0]),
]
for name,expr,wanted in edge:
    output=''.join(f'output[{i}u]=bitcast<u32>(f32(result'+('.'+'xyzw'[i] if len(wanted)>1 else '')+'));' for i in range(len(wanted)))
    save(name,OUT+'const result='+expr+';'+ENTRY+'{'+output+'}',expected_options=[[x] for x in wanted],output_words=8,obligation='Deterministic const signed-zero/subnormal/underflow/rounded-graph policy, not universal GPU payload guarantee')

for name,expr in [('sqrt_nonfinite','sqrt(vec2f(bitcast<f32>(0x7fc12345u),bitcast<f32>(0x7f800000u)))'),('normalize_zero','normalize(vec3f(0.0f))'),('exp_large','exp(vec2f(1000.0f,-1000.0f))')]:
    # Let snapshots force runtime and do not turn edge-domain inputs into const errors.
    if name=='normalize_zero':body='let input=vec3f(0.0f);let result=normalize(input);'
    elif name=='exp_large':body='let input=vec2f(1000.0f,-1000.0f);let result=exp(input);'
    else:body='let input=vec2f(bitcast<f32>(0x7fc12345u),bitcast<f32>(0x7f800000u));let result=sqrt(input);'
    save('latitude_'+name,ENTRY+'{'+body+'}',obligation='Runtime indeterminate/NaN/Inf/zero-normalize domain; validation-only, no finite answer oracle and no numerical execution claim')

(R/'NUMERIC_SUPPLEMENT.json').write_text(json.dumps({'created_at':datetime.now(timezone.utc).isoformat(),'candidate_observed':False,'cases':rows},indent=2,sort_keys=True)+'\n')
print(len(rows),'additive numeric cases; candidate not observed')
