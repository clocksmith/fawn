from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parent;rows=[]
ENTRY='@compute @workgroup_size(1) fn main()';OUT='@group(0) @binding(1) var<storage,read_write> output:array<u32>;'
def add(name,source,status,**other):
 p=ROOT/'parse_probe'/f'{name}.wgsl';p.parent.mkdir(exist_ok=True);p.write_text(source+'\n');rows.append(dict(id=name,source=str(p.relative_to(ROOT)),source_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),expected_status=status,**other))
for i,(expr,result) in enumerate([('a<b',True),('a<=b',True),('b>a',True),('b>=a',True),('a>b',False),('a<b && b>c',True),('(a<b)==(c<b)',True),('a<(b+1u)',True),('a < (b>>1u)',False),('(a << 1u) < b',False),('a<b || a>c',True)]):
 add(f'comparison_{i}',OUT+ENTRY+'{let a=2u;let b=4u;let c=3u;output[0]=u32('+expr+');}',0,expected_options=[[int(result)]],output_words=8,baseline_preserved=True)
for i,expr in enumerate(['dot<>','dot<f32,f32>(vec2f(),vec2f())','dot<f32>(;','dot<f32>vec2f()','dot<1u>(vec2f(),vec2f())','dot<f32+f32>(vec2f(),vec2f())','dot<vec2<f32>>(vec2f(),','dot<<f32>>(vec2f(),vec2f())']):
 add(f'malformed_{i}',ENTRY+'{let v='+expr+';}',2,baseline_preserved=True)
for i,(prefix,expr) in enumerate([('fn dot(a:vec2f,b:vec2f)->f32{return a.x+b.x;}','dot<f32>(vec2f(),vec2f())'),('struct dot { value:f32, }','dot<f32>(1.0f)'),('','dot<f32>(vec2f(),vec2f())'),('','min<vec2<f32>>(vec2f(),vec2f())'),('','cross<f32>(vec3f(),vec3f())')]):
 add(f'template_rejection_{i}',prefix+ENTRY+'{let result='+expr+';}',4)
add('lexical_template_shadow',ENTRY+'{let dot=1u;let result=dot<f32>(vec2f(),vec2f());}',4)
for n in [1,2,4,8,16,32,64]:
 expression='dot<'+'array<'*(n-1)+'f32'+'>'*n+'(vec2f(),vec2f())'
 add(f'type_depth_{n}',ENTRY+'{let result='+expression+';}',4,type_depth=n,expected_depth=n)
 if n>1:add(f'type_depth_{n}_one_short',ENTRY+'{let result='+expression+';}',3,type_depth=n-1,expected_pool=19,expected_minimum=n)
data={'phase':'Additive post-observation parser audit; expectations fixed before this probe executes','cases':rows};(ROOT/'PARSE_PROBE.json').write_text(json.dumps(data,indent=2,sort_keys=True)+'\n');paths=[ROOT/'prepare_parse_probe.py',ROOT/'run_parse_probe.py',ROOT/'PARSE_PROBE.json']+[ROOT/r['source'] for r in rows];(ROOT/'PARSE_PROBE.sha256').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(ROOT))+'\n' for p in paths));print(len(rows),hashlib.sha256((ROOT/'PARSE_PROBE.sha256').read_bytes()).hexdigest())
