"""Independent forward-DAG, sparse, payload, layout and real-source witnesses."""
from __future__ import annotations
from pathlib import Path
from fractions import Fraction
from fixtures_m10 import basic, positive, value, emit_components, SENTINEL
from model_m10 import Schema, resource, raw_words, expected_writes
from exact_numeric_m8 import round_bits, finite_value, fp_operation


def extras():
    cases=[]
    for n in (2,3,4):
        for typ in ('i32','f32'):
            vals=[-3,0,7,-11][:n]
            expected=[round_bits(-x,negative_zero=(x==0)) if typ=='f32' else -x&0xffffffff for x in vals]
            cases.append(basic(f'negate_{typ}_{n}','let v=-'+value(typ,n,vals)+';'+emit_components(typ,n),expected))
        for typ in ('i32','u32'):
            vals=[0,0x1234567,31,127][:n]
            cases.append(basic(f'complement_{typ}_{n}','let v=~'+value(typ,n,vals)+';'+emit_components(typ,n),[~x&0xffffffff for x in vals]))
        cases.append(basic(f'rgba_read_write_{n}',f'var a={value("u32",n,list(range(1,n+1)))};a.g=37u;let v=a.'+'rgba'[:n][::-1]+';'+emit_components('u32',n),list(reversed([1,37]+list(range(3,n+1))))))
        sc=Schema();rs=[resource('out',sc.array('u32',None),0,2),resource('raw',sc.array('u32',None),1)]
        vals=[0x80000000,0x7fc12345,0x7f800000,0xff800000][:n]
        src=f'vec{n}u('+','.join(f'raw[{i}]' for i in range(n))+')'
        body=f'let v=bitcast<vec{n}u>(bitcast<vec{n}f>({src}));'+emit_components('u32',n)
        cases.append(positive(f'bitcast_payload_vector_{n}',body,vals,schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*(n+2)),(0,1):raw_words(vals)},tags=('payload',)))
    for n in (2,3,4):
        for op in ('==','!='):
            av=[0,1,1,0][:n];bv=[1,1,0,0][:n]
            wanted=[int((a==b) if op=='==' else (a!=b)) for a,b in zip(av,bv)]
            cases.append(basic(f'bool_compare_{n}_{op}',f'let v={value("bool",n,av)}{op}{value("bool",n,bv)};'+emit_components('bool',n),wanted))
    cases += [
        basic('partition_two_one_one','let v=vec4(vec2(2u,3u),5u,7u);'+emit_components('u32',4),[2,3,5,7]),
        basic('partition_one_one_two','let v=vec4(2u,3u,vec2(5u,7u));'+emit_components('u32',4),[2,3,5,7]),
        basic('mixed_abstract_float_inference','const v=vec3(1,2.0,3);let a:vec3f=v;out[0]=bitcast<u32>(a.x);out[1]=bitcast<u32>(a.y);out[2]=bitcast<u32>(a.z);',[round_bits(1),round_bits(2),round_bits(3)]),
        basic('vector_u32_runtime_wrap','let a=vec3u(4294967295u,2147483648u,0u);let b=vec3u(1u,2147483648u,1u);let v=a+b;'+emit_components('u32',3),[0,0,1]),
        basic('vector_f32_independent_rounding','let a=vec3f(16777216.0f,1.0f,1.0f);let b=vec3f(1.0f,0x1p-24f,0x1.8p-23f);let v=a+b;'+emit_components('f32',3),[0x4b800000,0x3f800000,0x3f800002]),
    ]
    sc=Schema();sc.structure('First',[('b','Later')]);sc.structure('Later',[('x',sc.array('u32',3))])
    cases.append(positive('forward_nominal_dependency','let a=First(Later(array<u32,3>(5u,7u,11u)));out[0]=a.b.x[2];',[11],schema=sc,legacy=False))
    cases.append(basic('forward_const_extent_attribute','let x:array<u32,N>=array(3u,5u,7u);out[0]=x[N-1];',[7],prefix='const N=M+1;const M=2;'))
    for depth in (8,31,60):
        sc=Schema()
        for i in range(depth):sc.structure('S'+str(i),[('x','S'+str(i+1) if i+1<depth else 'u32')])
        body='var a:S0; a'+'.x'*depth+'=91u;out[0]=a'+'.x'*depth+';'
        cases.append(positive(f'nominal_chain_{depth}',body,[91],schema=sc,legacy=False,tags=('depth','dependency')))
    sc=Schema();a=sc.structure('A',[('x','f32')]);b=sc.structure('B',[('a',a,16,16),('b','f32')]);rs=[resource('out',sc.array('u32',None),0,2),resource('u',b,1,1,2)]
    cases.append(positive('uniform_nested_explicit_gap','out[0]=bitcast<u32>(u.a.x);out[1]=bitcast<u32>(u.b);',[round_bits(3),round_bits(11)],schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*4),(0,1):sc.pack(b,((round_bits(3),),round_bits(11)))},legacy=False))
    sc=Schema();root=sc.structure('P',[('v',sc.vec(3,'u32')),('x','u32')]);rs=[resource('a',root,1,2),resource('b',root,2)]
    vals=((2,3,5),7);other=((11,13,17),19)
    cases.append(positive('whole_resource_aggregate_copy','let saved=b;a=saved;',[],schema=sc,resources=rs,buffers={(0,1):sc.pack(root,vals),(0,2):sc.pack(root,other)},writes={(0,1):dict(enumerate(sc.pack(root,other)))},legacy=False))
    # A robust inner read yielding zero must still permit the outer index zero.
    sc=Schema();rs=[resource('out',sc.array('u32',None),0,2),resource('inner',sc.array('u32',None),1),resource('outer',sc.array('u32',None),2)]
    cases.append(positive('independent_nested_resource_bounds','out[0]=outer[inner[out[1]]];',[71],schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*4),(0,1):raw_words([1]),(0,2):raw_words([71,97])}))
    for index in (0,1,2,0xffffffff):
        sc=Schema();box=sc.structure('Box',[('v',sc.vec(3,'u32')),('stamp','u32')]);ar=sc.array(box,None)
        rs=[resource('out',sc.array('u32',None),0,2),resource('obj',ar,1,2),resource('idx',sc.array('u32',None),2)]
        vals=(((3,5,7),11),((13,17,19),23));data=sc.pack(ar,vals)
        want=list(vals[index][0])+[vals[index][1]] if index<2 else [0]*4
        writes={(0,0):expected_writes(want)}
        if index<2:writes[(0,1)]=expected_writes([31,37,41,43],index*16)
        body='let saved=obj[idx[0]];obj[idx[0]]=Box(vec3u(31u,37u,41u),43u);out[0]=saved.v.x;out[1]=saved.v.y;out[2]=saved.v.z;out[3]=saved.stamp;'
        cases.append(positive(f'whole_struct_robust_{index}',body,[],schema=sc,resources=rs,buffers={(0,0):raw_words([SENTINEL]*6),(0,1):data,(0,2):raw_words([index])},writes=writes,legacy=False,tags=('bounds','snapshot')))
    cases.append(basic('assignment_reference_before_rhs','out[0]=0u;var a:array<u32,2>;a[out[0]]=effect();out[2]=a[0];out[3]=a[1];',[1,0xA1B2C3D4,29,0],prefix='fn effect()->u32{out[0]=1u;return 29u;}'))
    # Target format checks apply to unused declarations in every mode.
    for name,source in [('unused_offset_u32','struct Huge{@size(4294967295u) a:u32,b:u32}'),('unused_stride_u32','struct Huge{@size(4294967295u) a:u32} struct Outer{x:array<Huge,2>}'),('layout_add_overflow','struct A{@size(4294967292u) a:u32} struct B{x:array<A,4294967295u>} struct C{x:array<B,2u>}')]:
        cases.append(dict(name=name,source=source+'@compute @workgroup_size(1) fn main(){}',status=3 if 'u32' in name else 4,tags=['target_limit','negative']))
    return cases


def witnesses():
    cases=[]
    for width,height in ((1,1),(3,2),(5,4)):
        for short in (False,True):
            count=width*height
            p=[round_bits(Fraction((i*7)%17-8,4)) for i in range(count)]
            d=[round_bits(Fraction((i*3)%11-5,8)) for i in range(count)]
            vel=[(round_bits(Fraction((i*5)%19-9,4)),round_bits(Fraction((i*11)%23-11,8))) for i in range(count)]
            if short:
                p=p[:max(1,count-2)];d=d[:max(1,count-1)];vel=vel[:max(1,count-3)]
            config=(width,height,17,0,round_bits(Fraction(1,32)),round_bits(1),round_bits(1),round_bits(2))
            def idx(x,y):return max(0,min(height-1,y))*width+max(0,min(width-1,x))
            for kernel in ('pressure','divergence','project'):
                sc=Schema();root=sc.structure('Config',[(k,'u32' if i<4 else 'f32') for i,k in enumerate(('width','height','seed','_pad0','dt','velocity_dissipation','dye_dissipation','force_scale'))]);af=sc.array('f32',None);av=sc.array(sc.vec(2,'f32'),None)
                rs=[resource('config',root,0,1,2)];buffers={(0,0):sc.pack(root,config)}
                if kernel=='pressure':
                    rs += [resource('divergence_in',af,1),resource('pressure_in',af,2),resource('pressure_out',af,3,2)];buffers.update({(0,1):raw_words(d),(0,2):raw_words(p)});outkey=(0,3)
                elif kernel=='divergence':
                    rs += [resource('velocity_in',av,1),resource('divergence_out',af,2,2)];buffers[(0,1)]=sc.pack(av,vel);outkey=(0,2)
                else:
                    rs += [resource('velocity_in',av,1),resource('pressure_in',af,2),resource('velocity_out',av,3,2)];buffers.update({(0,1):sc.pack(av,vel),(0,2):raw_words(p)});outkey=(0,3)
                wanted=[]
                for y in range(height):
                    for x in range(width):
                        ids=[idx(x-1,y),idx(x+1,y),idx(x,y-1),idx(x,y+1)];i=y*width+x
                        if kernel=='pressure':
                            l,r,b,t=(p[j] if j<len(p) else 0 for j in ids)
                            z=fp_operation('+',fp_operation('+',fp_operation('+',l,r),b),t)
                            wanted.append(fp_operation('*',fp_operation('-',z,d[i] if i<len(d) else 0),round_bits(Fraction(1,4))))
                        elif kernel=='divergence':
                            l,r,b,t=(vel[j] if j<len(vel) else (0,0) for j in ids)
                            wanted.append(fp_operation('*',round_bits(Fraction(1,2)),fp_operation('+',fp_operation('-',r[0],l[0]),fp_operation('-',t[1],b[1]))))
                        else:
                            l,r,b,t=(p[j] if j<len(p) else 0 for j in ids)
                            wanted += [fp_operation('-',vel[i][j] if i<len(vel) else 0,fp_operation('*',fp_operation('-',hi,lo),round_bits(Fraction(1,2)))) for j,(hi,lo) in enumerate(((r,l),(t,b)))]
                if short:
                    wanted=wanted[:max(1,count-1)*(2 if kernel=='project' else 1)]
                    buffers[outkey]=raw_words([SENTINEL]*len(wanted))
                else:buffers[outkey]=raw_words([SENTINEL]*(len(wanted)+4))
                source=(Path(__file__).parent/'witnesses'/f'stable_fluids_{kernel}.wgsl').read_text()
                cases.append(dict(name=f'real_{kernel}_{width}x{height}_{"short" if short else "full"}',source=source,status=0,schema=sc,resources=rs,buffers=buffers,writes={outkey:expected_writes(wanted)},invocations=[(x,y,0) for y in range(8) for x in range(8)],local=(8,8,1),tags=['real_witness'],legacy=kernel=='pressure',execute=True,reflection=sc.reflection(rs,local=(8,8,1))))
    return cases
