"""Undisclosed M10 independent source cases and source-schema expectations.

No production code/IR/reflection participates in case construction. Constants
with raw floating bits use the accepted independent nearest-even conversion.
"""
from __future__ import annotations
from fractions import Fraction
from dataclasses import dataclass
import hashlib
import json
from model_m10 import Schema, resource, declarations, raw_words, expected_writes
from exact_numeric_m8 import round_bits, finite_value

SENTINEL = 0xA1B2C3D4


def positive(name: str, body: str, wanted: list[int], prefix: str = '',
             schema: Schema | None = None, resources: list | None = None,
             buffers: dict | None = None, writes: dict | None = None,
             local: tuple = (1,1,1), invocations: list | None = None,
             tags: tuple = (), legacy: bool = True, execute: bool = True) -> dict:
    schema = Schema() if schema is None else schema
    if resources is None:
        resources = [resource('out',schema.array('u32',None),0,2)]
    nout = max(4,len(wanted)+2)
    buffers = {(0,0):raw_words([SENTINEL]*nout)} if buffers is None else buffers
    writes = {(0,0):expected_writes(wanted)} if writes is None else writes
    source = schema.declarations()+declarations(resources)+prefix
    source += '@compute @workgroup_size('+','.join(map(str,local))+') fn main(@builtin(global_invocation_id) gid:vec3<u32>){'+body+'}'
    return dict(name=name,source=source,status=0,schema=schema,resources=resources,buffers=buffers,writes=writes,
                invocations=[(0,0,0)] if invocations is None else invocations,local=local,tags=list(tags),
                legacy=legacy,execute=execute,reflection=schema.reflection(resources,local=local))


def basic(name: str, body: str, wanted: list[int], prefix: str = '', **kw) -> dict:
    return positive(name,body,wanted,prefix,**kw)


def encode(typ: str, expr: str) -> str:
    return f'u32({expr})' if typ in {'bool','u32'} else f'bitcast<u32>({expr})'


def emit_components(typ: str, n: int, name: str = 'v', start: int = 0) -> str:
    return ''.join(f'out[{start+i}]={encode(typ,name+"."+"xyzw"[i])};' for i in range(n))


def value(typ: str, n: int, vals: list[int]) -> str:
    suffix = {'u32':'u','i32':'i','f32':'.0f','bool':''}[typ]
    pieces = [('true' if x else 'false') if typ=='bool' else str(x)+suffix for x in vals]
    return f'vec{n}<{typ}>('+','.join(pieces)+')'


def bits(typ: str, x: int | Fraction) -> int:
    return round_bits(x) if typ=='f32' else int(x)&0xffffffff


def positives() -> list[dict]:
    cases=[]
    for n in (2,3,4):
        for typ in ('bool','i32','u32','f32'):
            vals = [int(i%2==0) for i in range(n)] if typ=='bool' else list(range(2,n+2))
            want = [bits(typ,x) for x in vals]
            for form,expr,w in [('zero',f'vec{n}<{typ}>()',[0]*n),
                                ('splat',f'vec{n}<{typ}>('+('true' if typ=='bool' else {'u32':'3u','i32':'3i','f32':'3.0f'}[typ])+')',[bits(typ,1 if typ=='bool' else 3)]*n),
                                ('pieces',value(typ,n,vals),want)]:
                cases.append(basic(f'vector_{form}_{typ}_{n}',f'var v={expr};'+emit_components(typ,n),w,tags=('constructor','vector')))
            cases.append(basic(f'vector_value_copy_{typ}_{n}',f'var a={value(typ,n,vals)};let v=a;a.x='+('false' if typ=='bool' else {'u32':'99u','i32':'99i','f32':'99.0f'}[typ])+';'+emit_components(typ,n),want,tags=('snapshot',)))
            rev='xyzw'[:n][::-1]
            cases.append(basic(f'swizzle_reverse_{typ}_{n}',f'let a={value(typ,n,vals)};let v=a.{rev};'+emit_components(typ,n),list(reversed(want)),tags=('swizzle',)))
            cases.append(basic(f'swizzle_repeat_{typ}_{n}',f'let a={value(typ,n,vals)};let v=a.'+'x'*4+';'+emit_components(typ,4),[want[0]]*4,tags=('swizzle',)))
            for dst in ('bool','i32','u32','f32'):
                converted=[int(x!=0) if dst=='bool' else x for x in vals]
                cases.append(basic(f'vector_convert_{typ}_{dst}_{n}',f'let a={value(typ,n,vals)};let v=vec{n}<{dst}>(a);'+emit_components(dst,n),[bits(dst,x) for x in converted],tags=('conversion',)))
        for typ in ('i32','u32','f32'):
            left=list(range(7,7+n));right=list(range(2,2+n))
            for op in ('+','-','*','/','%'):
                for shape in ('vv','sv','vs'):
                    av=left if shape!='sv' else [9]*n
                    bv=right if shape!='vs' else [3]*n
                    answers=[]
                    for a,b in zip(av,bv):
                        if op=='+': z=a+b
                        elif op=='-': z=a-b
                        elif op=='*': z=a*b
                        elif op=='/': z=Fraction(a,b) if typ=='f32' else a//b
                        else: z=a-(a//b)*b
                        answers.append(bits(typ,z))
                    scalar=lambda x: str(x)+{'i32':'i','u32':'u','f32':'.0f'}[typ]
                    lhs=scalar(9) if shape=='sv' else value(typ,n,left)
                    rhs=scalar(3) if shape=='vs' else value(typ,n,right)
                    cases.append(basic(f'vector_arith_{typ}_{n}_{ord(op)}_{shape}',f'let a={lhs};let b={rhs};let v=a{op}b;'+emit_components(typ,n),answers,tags=('arithmetic',)))
            for op in ('==','!=','<','<=','>','>='):
                av=list(range(n));bv=list(reversed(range(n)))
                tests={'==':lambda a,b:a==b,'!=':lambda a,b:a!=b,'<':lambda a,b:a<b,'<=':lambda a,b:a<=b,'>':lambda a,b:a>b,'>=':lambda a,b:a>=b}
                cases.append(basic(f'compare_{typ}_{n}_{op}',f'let a={value(typ,n,av)};let b={value(typ,n,bv)};let v=a{op}b;'+emit_components('bool',n),[int(tests[op](a,b)) for a,b in zip(av,bv)],tags=('comparison',)))
        for typ in ('u32','i32'):
            av=[19,37,61,89][:n];bv=[5,10,17,23][:n]
            for op in ('&','|','^'):
                fn={'&':lambda a,b:a&b,'|':lambda a,b:a|b,'^':lambda a,b:a^b}[op]
                cases.append(basic(f'bits_{typ}_{n}_{ord(op)}',f'let a={value(typ,n,av)};let b={value(typ,n,bv)};let v=a{op}b;'+emit_components(typ,n),[fn(a,b) for a,b in zip(av,bv)],tags=('bitwise',)))
        cases.append(basic(f'bool_not_{n}',f'let v=!{value("bool",n,[0,1,0,1][:n])};'+emit_components('bool',n),[1,0,1,0][:n]))
        for op in ('&','|'):
            av=[0,0,1,1][:n];bv=[0,1,0,1][:n]
            cases.append(basic(f'bool_eager_{n}_{ord(op)}',f'let a={value("bool",n,av)};let b={value("bool",n,bv)};let v=a{op}b;'+emit_components('bool',n),[(a&b if op=='&' else a|b) for a,b in zip(av,bv)]))
    partitions=[('vec3(1,vec2(2,3))',[1,2,3]),('vec3(vec2(4,5),6)',[4,5,6]),
                ('vec4(vec2(1,2),vec2(3,4))',[1,2,3,4]),('vec4(1,vec2(2,3),4)',[1,2,3,4]),
                ('vec4(vec3(4,3,2),1)',[4,3,2,1]),('vec4(4,vec3(3,2,1))',[4,3,2,1])]
    for index,(expr,vals) in enumerate(partitions):
        cases.append(basic(f'inferred_partition_{index}',f'const v={expr};'+emit_components('i32',len(vals)),vals,tags=('abstract','constructor')))
    cases += [
        basic('abstract_vector_large_projection','const v=vec2(2147483648,7);out[0]=u32(v.x);out[1]=u32(v.y);',[0x80000000,7]),
        basic('abstract_vector_materialize_context','const v=vec2(2147483647,2)+vec2(1,3);let u:vec2<u32>=v;out[0]=u.x;out[1]=u.y;',[0x80000000,5]),
        basic('abstract_array_nested_projection','const a=array(array(2147483648,7),array(8,9));out[0]=u32(a[0][0]);out[1]=u32(a[1][1]);',[0x80000000,9]),
        basic('inferred_zero_vector','const v=vec4();'+emit_components('i32',4),[0]*4),
        basic('array_value_snapshot','var a=array<u32,4>(2u,5u,11u,17u);let b=a;a[0]=99u;out[0]=b[0];out[1]=a[0];out[2]=b[3];',[2,99,17]),
        basic('array_count_value_identity','var a:array<u32,2u>=array<u32,1+1>(9u,13u);out[0]=a[0];out[1]=a[1];',[9,13]),
        basic('array_zero_and_inferred','var a:array<vec3<u32>,3>;a[1]=vec3u(4u,7u,9u);let b=array(a[0],a[1],a[2]);out[0]=b[0].y;out[1]=b[1].z;out[2]=b[2].x;',[0,9,0]),
        basic('array_function_argument_result','let a=array<u32,3>(4u,9u,16u);let b=f(a);out[0]=b[0];out[1]=b[1];out[2]=a[1];',[4,19,9],prefix='fn f(x:array<u32,3>)->array<u32,3>{var y=x;y[1]+=10u;return y;}'),
        basic('rvalue_array_index_loop_refresh','var total=0u;for(var i=0u;i<3u;i++){total+=f(i)[i];}out[0]=total;',[34],prefix='fn f(x:u32)->array<u32,3>{return array<u32,3>(x+1u,x+10u,x+20u);}'),
        basic('constructor_argument_effect_order','out[0]=0u;let v=vec3u(step(),step(),step());out[1]=v.x;out[2]=v.y;out[3]=v.z;',[3,1,2,3],prefix='fn step()->u32{out[0]++;return out[0];}'),
        basic('array_argument_effect_order','out[0]=0u;let v=array(step(),step(),step());out[1]=v[0];out[2]=v[1];out[3]=v[2];',[3,1,2,3],prefix='fn step()->u32{out[0]++;return out[0];}'),
        basic('byvalue_later_argument_mutates','out[0]=3u;out[1]=4u;let v=pair(vec2u(out[0],out[1]),effect());out[2]=v.x;out[3]=v.y;',[90,4,3,6],prefix='fn effect()->u32{out[0]=90u;return 2u;}fn pair(a:vec2u,b:u32)->vec2u{return a+vec2u(0u,b);}'),
        basic('constructor_scope_shadow','out[0]=vec2u(3u);',[12],prefix='fn vec2u(x:u32)->u32{return x*4u;}'),
        basic('array_large_zero_sparse','var a:array<u32,1000000>;a[999999]=17u;out[0]=a[999999];out[1]=a[0];',[17,0],tags=('sparse','capacity'),legacy=False),
    ]
    # Runtime exceptional lanes originate in storage, never in a constant expression.
    for n in (2,3,4):
        sc=Schema();rs=[resource('out',sc.array('u32',None),0,2),resource('src',sc.array('u32',None),1)]
        av=[0x80000000,7,0xfffffff7,23][:n];bv=[0xffffffff,0,2,3][:n]
        a='vec'+str(n)+'i('+','.join(f'bitcast<i32>(src[{i}])' for i in range(n))+')'
        b='vec'+str(n)+'i('+','.join(f'bitcast<i32>(src[{n+i}])' for i in range(n))+')'
        for op in ('/','%'):
            want=[0x80000000 if op=='/' else 0,7 if op=='/' else 0,0xfffffffc if op=='/' else 0xffffffff,7 if op=='/' else 2][:n]
            cases.append(positive(f'guarded_signed_{n}_{ord(op)}',f'let a={a};let b={b};let v=a{op}b;'+emit_components('i32',n),want,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*(n+2)),(0,1):raw_words(av+bv)},tags=('safe_lanes',)))
        av=[0xffffffff,0x80000000,0x12345678,0x55][:n];counts=[32,33,0xffffffff,2][:n]
        aa=f'vec{n}u('+','.join(f'src[{i}]' for i in range(n))+')';bb=f'vec{n}u('+','.join(f'src[{n+i}]' for i in range(n))+')'
        for op in ('<<','>>'):
            want=[((a<<(b%32)) if op=='<<' else a>>(b%32))&0xffffffff for a,b in zip(av,counts)]
            cases.append(positive(f'guarded_shift_{n}_{op}',f'let a={aa};let b={bb};let v=a{op}b;'+emit_components('u32',n),want,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*(n+2)),(0,1):raw_words(av+counts)},tags=('safe_lanes',)))
        raw=[0x7fc12345,0x7f800000,0xff800000,0x3fc00000][:n]
        expr=f'vec{n}f('+','.join(f'bitcast<f32>(src[{i}])' for i in range(n))+')'
        for dst in ('i32','u32'):
            want=([0,0x7fffff80,0x80000000,1] if dst=='i32' else [0,0xffffff00,0,1])[:n]
            cases.append(positive(f'guarded_float_convert_{n}_{dst}',f'let a={expr};let v=vec{n}<{dst}>(a);'+emit_components(dst,n),want,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*(n+2)),(0,1):raw_words(raw)},tags=('safe_lanes','conversion')))
    return cases


def layouts_and_access() -> list[dict]:
    cases=[]
    for n in (2,3,4):
        sc=Schema();v=sc.vec(n,'f32');inner=sc.structure('Inner',[('v',v),('stamp','u32')]);ar=sc.array(inner,3)
        outer=sc.structure('Outer',[('tag','u32'),('items',ar),('last','u32')]);rs=[resource('out',sc.array('u32',None),0,2),resource('obj',outer,1,2)]
        vals=(31,tuple((tuple(round_bits((i+1)*7+j) for j in range(n)),100+i) for i in range(3)),47)
        data=sc.pack(outer,vals);lay=sc.layouts();offset=lay[outer]['members'][1]['offset']+lay[ar]['stride']+lay[inner]['members'][1]['offset']
        writes={(0,0):expected_writes([round_bits(14),101,47]),(0,1):expected_writes([209],offset)}
        cases.append(positive(f'nested_layout_vec{n}','let saved=obj;out[0]=bitcast<u32>(saved.items[1].v.x);out[1]=saved.items[1].stamp;obj.items[1].stamp=209u;out[2]=obj.last;',[],schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*5),(0,1):data},writes=writes,legacy=False,tags=('layout','copy','nested')))
    for n in (2,3,4):
        sc=Schema();v=sc.vec(n,'u32');rs=[resource('out',sc.array('u32',None),0,2),resource('src',sc.array(v,None),1)]
        source_values=tuple(tuple(10*i+j+1 for j in range(n)) for i in range(3));data=sc.pack(rs[1]['type'],source_values)
        for ix in (-1,0,2,3,0x7fffffff):
            buffers={(0,0):raw_words([SENTINEL]*(n+3)),(0,1):data,(0,2):raw_words([ix&0xffffffff])}
            rr=rs+[resource('idx',sc.array('i32',None),2)]
            want=list(source_values[ix]) if 0<=ix<3 else [0]*n
            cases.append(positive(f'runtime_vector_load_{n}_{ix}','let v=src[idx[0]];'+emit_components('u32',n),want,schema=sc,resources=rr,buffers=buffers,legacy=False,tags=('bounds','layout')))
    sc=Schema();v=sc.vec(3,'u32');tail=sc.array(v,None);root=sc.structure('Tail',[('head','u32',32),('data',tail)])
    rs=[resource('out',sc.array('u32',None),0,2),resource('b',root,1,2),resource('idx',sc.array('u32',None),2)]
    for count in (0,1,3):
        size=sc.layouts()[root]['tail'][1]+count*sc.layouts()[tail]['stride']+7
        vals=(71,tuple((10+i,20+i,30+i) for i in range(count)))
        for ix in dict.fromkeys((0,max(0,count-1),count,0xffffffff)):
            data=sc.pack(root,vals,size=size) if count else bytes([0xD7]*size)
            if not count: data=raw_words([71])+data[4:]
            wanted=[71]+(list(vals[1][ix]) if ix<count else [0,0,0])
            body='let v=b.data[idx[0]];out[0]=b.head;'+emit_components('u32',3,start=1)
            cases.append(positive(f'runtime_tail_{count}_{ix}',body,wanted,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*6),(0,1):data,(0,2):raw_words([ix])},legacy=False,tags=('tail','bounds','logical_short_binding' if count==0 else 'tail_range')))
    # Direct scalar/vector/fixed-array roots plus explicit uniform layout.
    for kind,root,vals,body,want in [('scalar','i32',0xfffffff9,'out[0]=bitcast<u32>(a);',[0xfffffff9]),
                                    ('vector','vec3<u32>',(17,23,31),'out[0]=a.z;',[31]),
                                    ('array','array<u32,3>',(17,23,31),'out[0]=a[1];',[23])]:
        sc=Schema();sc.vec(3,'u32');sc.array('u32',3)
        rs=[resource('out',sc.array('u32',None),0,2),resource('a',root,1)]
        cases.append(positive('direct_root_'+kind,body,want,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*4),(0,1):sc.pack(root,vals)},legacy=False))
    sc=Schema();inner=sc.structure('Padded',[('x','f32',0,16)]);arr=sc.array(inner,3);outer=sc.structure('Uniform',[('prefix','u32',0,16),('a',arr),('last','u32')]);rs=[resource('out',sc.array('u32',None),0,2),resource('cfg',outer,1,1,2)]
    vals=(19,((round_bits(2),),(round_bits(3),),(round_bits(5),)),29)
    cases.append(positive('uniform_explicit_nested','out[0]=cfg.prefix;out[1]=bitcast<u32>(cfg.a[1].x);out[2]=cfg.last;',[19,round_bits(3),29],schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*5),(0,1):sc.pack(outer,vals)},legacy=False,tags=('uniform','layout')))
    # Readonly source projection after caller-owned mutable local updates.
    sc=Schema();inn=sc.structure('VecBox',[('v',sc.vec(3,'u32')),('flag','u32')]);root=sc.structure('Nest',[('p',sc.array(inn,2)),('end','u32')])
    cases.append(positive('nested_byvalue_result','var a=Nest(array<VecBox,2>(VecBox(vec3u(2u,3u,5u),7u),VecBox(vec3u(11u,13u,17u),19u)),23u);let b=change(a);out[0]=a.p[1].v.y;out[1]=b.p[1].v.y;out[2]=b.p[0].flag;out[3]=b.end;',[13,42,7,23],prefix='fn change(x:Nest)->Nest{var y=x;y.p[1].v.y=42u;return y;}',schema=sc,legacy=False,tags=('snapshot','nested')))
    # Local nested access guards and exact evaluation order use a separate state buffer.
    sc=Schema();rs=[resource('out',sc.array('u32',None),0,2),resource('ix',sc.array('u32',None),1)]
    for ix in (0,1,2,0xffffffff):
        body='var a=array<array<vec2u,2>,2>();a[1][1]=vec2u(7u,11u);let v=a[ix[0]][1];out[0]=v.x;out[1]=v.y;a[ix[0]][1].x=23u;out[2]=a[1][1].x;'
        want=([7,11] if ix==1 else [0,0])+[23 if ix==1 else 7]
        cases.append(positive(f'nested_local_bounds_{ix}',body,want,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*5),(0,1):raw_words([ix])},tags=('bounds','nested')))
    cases += [
      basic('compound_snapshot_before_rhs','out[0]=5u;out[0]+=effect();out[1]=out[0];',[7,7],prefix='fn effect()->u32{out[0]=90u;return 2u;}'),
      basic('compound_vector_snapshot_before_rhs','var a=vec2u(5u,7u);let v=a;a+=vec2u(2u,3u);out[0]=a.x;out[1]=v.x;out[2]=a.y;',[7,5,10]),
      basic('nested_oob_still_later_index_effect','out[0]=0u;var a:array<array<u32,2>,2>;let bad=out[3];a[bad][effect()]=effect();out[1]=out[0];',[2,2],prefix='fn effect()->u32{out[0]++;return 0u;}'),
      basic('index_value_captured_once','out[0]=0u;var a:array<u32,3>;a[effect()]+=9u;out[1]=a[0];out[2]=out[0];',[1,9,1],prefix='fn effect()->u32{let old=out[0];out[0]++;return old;}'),
      basic('storage_load_after_call','out[0]=3u;let a=vec2u(out[0]);change();let b=vec2u(out[0]);out[1]=a.x;out[2]=b.y;',[13,3,13],prefix='fn change(){out[0]=13u;}'),
      basic('swizzle_assignment_snapshot','var a=vec3u(2u,5u,11u);a=vec3u(a.zyx);out[0]=a.x;out[1]=a.y;out[2]=a.z;',[11,5,2]),
    ]
    return cases


def negatives() -> list[dict]:
    rows=[]
    def bad(name: str, body: str = '', prefix: str = '', status: int = 4) -> None:
        source='@group(0) @binding(0) var<storage,read_write> out:array<u32>;'+prefix+'@compute @workgroup_size(1) fn main(){'+body+'}'
        rows.append(dict(name=name,source=source,status=status,tags=['negative']))
    for n in (2,3,4):
        bad(f'vector_arity_{n}',f'let x=vec{n}u('+','.join('1u' for _ in range(n+1))+');')
        bad(f'vector_bool_numeric_{n}',f'let x=vec{n}<bool>(1u);')
        bad(f'vector_mixed_concrete_{n}',f'let x=vec{n}f('+','.join(['1u']+['2.0f']*(n-1))+');')
        bad(f'vector_bad_condition_{n}',f'if(vec{n}<bool>(true)){{}}')
        bad(f'vector_increment_{n}',f'var x=vec{n}u();x++;')
        bad(f'vector_compare_scalar_{n}',f'let x=vec{n}u(1u)==1u;')
        bad(f'vector_shift_scalar_{n}',f'let x=vec{n}u(1u)<<1u;')
        bad(f'vector_signed_shift_count_{n}',f'let x=vec{n}u(1u)<<vec{n}i(1i);')
        bad(f'vector_bool_xor_{n}',f'let x=vec{n}<bool>(true)^vec{n}<bool>(false);')
        bad(f'vector_logic_short_{n}',f'let x=vec{n}<bool>(true)&&vec{n}<bool>(false);')
        bad(f'vector_unsigned_negation_{n}',f'let x=-vec{n}u(1u);')
        bad(f'vector_bad_const_lane_div_{n}',f'const x=vec{n}u(1u)/'+value('u32',n,[1]*(n-1)+[0])+';')
        bad(f'vector_bad_const_index_{n}',f'let x=vec{n}u(1u)[{n}];')
        bad(f'vector_negative_const_index_{n}',f'let x=vec{n}u(1u)[-1];')
    for name,body,prefix in [
      ('swizzle_mixed_alphabet','let x=vec4u().xr;',''),('swizzle_outside_width','let x=vec2u().z;',''),
      ('swizzle_write_multi','var x=vec2u();x.xy=vec2u(1u);',''),
      ('array_no_inference','let x=array();',''),('array_copy_ctor','let a=array(1u,2u);let b=array<u32,2>(a);',''),
      ('array_wrong_arity','let a=array<u32,3>(1u,2u);',''),('array_heterogeneous','let a=array(1u,2i);',''),
      ('array_arithmetic','let a=array(1u,2u)+array(2u,3u);',''),('array_equality','let a=array(1u,2u)==array(1u,2u);',''),
      ('array_const_oob','let a=array(1u,2u);let b=a[2];',''),('array_zero_length','var a:array<u32,0>;',''),
      ('array_negative_length','var a:array<u32,-1>;',''),('array_float_length','var a:array<u32,2.0>;',''),
      ('array_runtime_local','var a:array<u32>;',''),('abstract_runtime_index_concretizes','const a=array(2147483648,1);let i=0u;out[0]=u32(a[i]);',''),
      ('formal_write','let a=f(vec2u());','fn f(x:vec2u)->vec2u{x.x=3u;return x;}'),
      ('let_nested_write','let a=array(vec2u());a[0].x=2u;',''),
      ('nominal_mismatch','let a:A=B(1u);','struct A{x:u32} struct B{x:u32}'),
      ('struct_copy_ctor','let a=A(1u);let b=A(a);','struct A{x:u32}'),
      ('struct_missing_field','let a=A(1u);','struct A{x:u32,y:u32}'),
      ('struct_bad_member','let a=A();out[0]=a.nope;','struct A{x:u32}'),
      ('struct_duplicate_member','','struct A{x:u32,x:f32}'),('struct_cycle','','struct A{x:A}'),
      ('struct_mutual_cycle','','struct A{x:B} struct B{x:A}'),
      ('extent_type_cycle','','struct A{x:array<u32,N>} const N=A().x[0]+1u;'),
      ('attribute_const_cycle','','struct A{@size(N) x:u32} const N=A().x+4u;'),
      ('unused_bad_attribute','','struct A{@align(3) x:u32}'),('align_below_natural','','struct A{@align(4) x:vec3f}'),
      ('size_below_natural','','struct A{@size(8) x:vec3f}'),('attribute_size_zero','','struct A{@size(0) x:u32}'),
      ('attribute_duplicate','','struct A{@align(8) @align(8) x:u32}'),('tail_not_last','','struct A{x:array<u32>,y:u32}'),
      ('nested_tail','','struct A{x:array<u32>} struct B{x:A}'),('array_of_tail','','struct A{x:array<u32>} struct B{x:array<A,2>}'),
      ('tail_explicit_size','','struct A{@size(16) x:array<u32>}'),
      ('tail_as_argument','','struct A{x:array<u32>} fn f(x:A){}'),('tail_as_result','','struct A{x:array<u32>} fn f()->A{return A();}'),
      ('uniform_bool','','struct A{x:bool} @group(0) @binding(1) var<uniform> a:A;'),
      ('storage_bool','','@group(0) @binding(1) var<storage> a:array<vec2<bool>>;'),
      ('uniform_scalar_array','','@group(0) @binding(1) var<uniform> a:array<f32,2>;'),
      ('uniform_nested_spacing','','struct A{x:u32} struct B{a:A,b:u32} @group(0) @binding(1) var<uniform> x:B;'),
      ('readonly_field_write','a.x=7u;','struct A{x:u32} @group(0) @binding(1) var<storage,read> a:A;'),
      ('unused_bad_function','','fn unused()->vec2u{return vec3u();}'),
      ('shadow_type_generator','let vec2u=1u;let x=vec2u(2u);',''),
    ]: bad(name,body,prefix)
    for name,prefix,body in [
      ('matrix','', 'let a=mat2x2f();'),('f16','enable f16;','let a=vec2h();'),
      ('workgroup','var<workgroup> a:array<u32,4>;',''),('private','var<private> a:vec2u;',''),
      ('pointer','','var a=1u;let p=&a;'),('arraylength','', 'out[0]=arrayLength(&out);'),
      ('vector_math','','let a=min(vec2u(1u),vec2u(2u));'),('vector_dot','','let a=dot(vec2f(),vec2f());'),
      ('alias','alias A=vec2u;',''),('override','override N:u32=2u;','var a:array<u32,N>;')
    ]: bad(name,body,prefix,status=3)
    return rows


def all_cases() -> list[dict]:
    result=positives()+layouts_and_access()+negatives()
    names=[x['name'] for x in result]
    if len(set(names))!=len(names):
        raise ValueError('duplicate independent case name')
    return result


def inventory(cases: list[dict]) -> list[dict]:
    rows=[]
    for c in cases:
        row={k:c[k] for k in ('name','status','tags')}
        row['source_sha256']=hashlib.sha256(c['source'].encode()).hexdigest()
        if c['status']==0:
            expectation=dict(reflection=c['reflection'],buffers={str(k):v.hex() for k,v in c['buffers'].items()},
                             writes={str(k):v for k,v in c['writes'].items()},invocations=c['invocations'],legacy=c['legacy'],execute=c['execute'])
            row['expectation_sha256']=hashlib.sha256(json.dumps(expectation,sort_keys=True).encode()).hexdigest()
        rows.append(row)
    return rows
