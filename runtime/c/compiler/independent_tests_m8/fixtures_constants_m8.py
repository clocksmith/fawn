"""Independent declaration/dependency/attribute/signed-index cases for M8."""
from __future__ import annotations

from fixtures_scalar_m8 import make, S, U
from fixtures_m5 import storage, uniform, shape, packed
from fixtures_m6 import program, trial
from exact_numeric_m8 import signed


def positives() -> list:
    cases=[]
    cases.append(make('module_forward_diamond','out[0]=u32(root);',[19],
        prefix='const root=left+right;const right=base*3;const left=base+3;',suffix='const base=4;'))
    cases.append(make('module_const_all_types','out[0]=u32(a);out[1]=b;out[2]=bitcast<u32>(c);out[3]=u32(d);',[U,17,0x3fc00000,1],
        prefix='const a:i32=-1;const b:u32=17;const c:f32=1.5;const d:bool=true;'))
    cases.append(make('module_constant_empty_declarations','out[0]=u32(a+b);',[7],
        prefix=';;;const a=3;;',suffix=';;;const b=4;;;;'))
    for count in [2,33,257,1024]:
        prefix=''.join(f'const c{i}=c{i+1}+1;' for i in range(count-1))+f'const c{count-1}=1;'
        case=make(f'iterative_constant_chain_{count}','out[0]=u32(c0);',[count],prefix=prefix)
        case[2]['job_only']=count>33
        cases.append(case)
    cases.append(make('module_unused_valid_large_abstract','out[0]=1u;',[1],prefix='const big=9223372036854775807;'))
    # A local initializer sees an outer binding, never itself.
    cases.append(make('local_const_shadow_module','const a=a+2;out[0]=u32(a);',[5],prefix='const a=3;'))
    cases.append(make('local_const_binds_after_initializer','const a=3;{const a=a*2;out[0]=u32(a);}out[1]=u32(a);',[6,3]))
    cases.append(make('const_reference_remains_abstract','out[0]=u32(a);',[U],prefix='const a=4294967295;'))
    cases.append(make('const_reference_remains_concrete','out[0]=u32(a+1i);',[0x80000000],prefix='const a:i32=2147483647;'))
    for name,group,binding,size,dims,prefix in [
        ('forward_attributes',3,31,'WX,WY,WZ',(4,4,16),'const G=3;const B=31;const WX=2+2;const WY=8/2;const WZ=1u<<4u;'),
        ('signed_attributes',0,0,'DIM',(256,1,1),'const G:i32=0;const B:i32=0;const DIM:i32=256;'),
        ('arithmetic_attributes',2,7,'4*4,2+2,4',(16,4,4),'const G=6/3;const B=(1u<<3u)-1u;')]:
        source,expected=program('out[0]=9u;',bindings=[storage('out',binding,group=group)],
                               parameters=[],size=size,dimensions=dims)
        source=source.replace(f'@group({group})'.encode(),b'@group(G)').replace(f'@binding({binding})'.encode(),b'@binding(B)')+prefix.encode()
        expected['vectors']=[trial({(group,binding):[S,S]}, {(group,binding):{0:9}})]
        cases.append((name,source,expected))
    # Declaration-order independent flat types and values after the entry.
    structs=[shape('Cfg',[('x',1)])]
    bindings=[storage('out',0),uniform('cfg',1,0,1)]
    source,expected=program('out[0]=cfg.x+N;',bindings=bindings,structs=structs,parameters=[])
    declaration_end=source.index(b'@compute')
    source=source[declaration_end:]+source[:declaration_end]+b'const N=3u;'
    expected['vectors']=[trial({(0,0):[S,S]}, {(0,0):{0:10}},uniforms={(0,1):packed([7])})]
    cases.append(('all_declarations_after_entry',source,expected))
    for count in [1,4]:
        cases.append(make(f'local_const_loop_scope_{count}',
            f'for(var i=0;i<{count};i++){{const k=3;out[i]=u32(i+k);}}',list(range(3,3+count))))
    for label,body in [
        ('signed_index_read','let i=bitcast<i32>(src[0]);out[0]=src[i];'),
        ('signed_index_store','let i=bitcast<i32>(src[0]);out[i]=17u;'),
        ('signed_index_wrap','let i=bitcast<i32>(src[0]);out[0]=src[i+1i];'),
        ('signed_nested_oob_zero','let i=bitcast<i32>(src[0]);out[0]=src[i32(src[i])];'),
        ('signed_compound_independent','let i=bitcast<i32>(src[0]);out[i]/=src[1];')]:
        vectors=[]
        for word in [0,1,2,3,U,0x80000000,0x7fffffff]:
            for length in [0,1,4]:
                a=[word,2,77]
                initial={(0,0):[13]*length,(0,1):a}
                index=signed(word)
                read=lambda i:a[i] if 0<=i<len(a) else 0
                if label=='signed_index_read':changes={0:read(index)}
                elif label=='signed_index_store':changes={index:17}
                elif label=='signed_index_wrap':changes={0:read(signed(word+1))}
                elif label=='signed_nested_oob_zero':changes={0:read(signed(read(index)))}
                else:changes={index:6}
                vectors.append(trial(initial,{(0,0):changes}))
        cases.append(make(label,body,vectors=vectors))
    return cases


def negatives() -> list:
    cases=[]
    base=program('',bindings=[storage('out',0)],parameters=[])[0]
    def add(name: str, text: str, status: tuple=(4,)) -> None:
        cases.append((name,text.encode(),status))
    for name,prefix in [
        ('module_self_cycle','const a=a;'),('module_two_cycle','const a=b;const b=a;'),
        ('module_diamond_cycle','const a=b+c;const b=d;const c=d;const d=a;'),
        ('module_skip_cycle','const a=false && b;const b=a;'),
        ('module_unused_bad_type','const unused:bool=1u;'),
        ('module_unused_bad_numeric','const unused=1u/0u;'),
        ('module_unused_missing','const unused=missing;'),
        ('module_const_duplicate','const a=1;const a=2;'),
        ('module_const_resource_duplicate','const out=1;'),
        ('module_const_entry_duplicate','const main=1;'),
        ('module_const_constructor_shadow','const u32=1;const a:u32=1;'),
        ('module_forward_type_shadow','const a:i32=1;const i32=2;'),
        ('module_forward_constructor_shadow','const a=u32(1);const u32=2;'),
        ('module_forward_max_shadow','const a=max(1,2);const max=2;'),
        ('module_forward_bitcast_shadow','const a=bitcast<u32>(1f);const bitcast=2;'),
        ('module_resource_reference','const a=out[0];'),
        ('module_uniform_type_shadow','const Cfg=1;struct Cfg{x:u32,}'),
    ]:
        add(name,prefix+base.decode())
    chain=''.join(f'const c{i}=c{i+1};' for i in range(257))+'const c257=c0;'
    add('module_long_cycle',chain+base.decode())
    for attribute,values in [('group',['-1','4','true','1f','missing','1u/0u']),
                             ('binding',['-1','32','false','1.0','missing']),
                             ('workgroup_size',['0','257','-1','true','1f','missing','16,16,2','1,1,0'])]:
        for i,value in enumerate(values):
            text=base.decode().replace('@'+attribute+'(0)', '@'+attribute+'('+value+')') if attribute!='workgroup_size' else base.decode().replace('@workgroup_size(1)','@workgroup_size('+value+')')
            add('invalid_attribute_'+attribute+'_'+str(i),text)
    for name,text in [
        ('resource_i32','@group(0) @binding(0) var<storage,read_write> out:array<i32>;@compute @workgroup_size(1) fn main(){}'),
        ('resource_bool','@group(0) @binding(0) var<storage,read_write> out:array<bool>;@compute @workgroup_size(1) fn main(){}'),
        ('uniform_i32','struct Cfg{x:i32,}@group(0) @binding(0) var<uniform> cfg:Cfg;@compute @workgroup_size(1) fn main(){}'),
        ('uniform_bool','struct Cfg{x:bool,}@group(0) @binding(0) var<uniform> cfg:Cfg;@compute @workgroup_size(1) fn main(){}')]:
        add(name,text,(3,4))
    return cases
