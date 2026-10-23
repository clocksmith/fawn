"""Independent M5 WGSL fixtures, host bytes and contract-derived effects."""
from __future__ import annotations
from pathlib import Path
import struct
from rational_oracle import operation

U = 0xffffffff
S = 0xa55a36c9

def storage(name: str, binding: int, scalar: int = 1, access: int = 2, group: int = 0) -> dict:
    return dict(name=name,group=group,binding=binding,kind=1,access=access,scalar=scalar,element_stride=4,runtime_sized=1,minimum_binding_size_bytes=4,type_alignment_bytes=4,address_space_alignment_bytes=4,struct_index=U)

def uniform(name: str, binding: int, index: int, count: int, group: int = 0) -> dict:
    return dict(name=name,group=group,binding=binding,kind=2,access=1,scalar=0,element_stride=0,runtime_sized=0,minimum_binding_size_bytes=count*4,type_alignment_bytes=4,address_space_alignment_bytes=16,struct_index=index)

def shape(name: str, members: list[tuple[str,int]]) -> dict:
    return dict(name=name,member_count=len(members),size_bytes=len(members)*4,alignment_bytes=4,members=[dict(name=n,scalar=t,offset_bytes=i*4,size_bytes=4,alignment_bytes=4) for i,(n,t) in enumerate(members)])

def declaration(spec: dict) -> str:
    return 'struct '+spec['name']+' { '+', '.join(m['name']+': '+('u32' if m['scalar']==1 else 'f32') for m in spec['members'])+', }\n'

def program(structs: list[dict], bindings: list[dict], body: str, reorder: bool = False, n: int = 1, entry: str = 'main') -> tuple[bytes,dict]:
    declarations=''.join(declaration(s) for s in structs)
    resources=''
    for b in bindings:
        kind='uniform' if b['kind']==2 else 'storage, '+('read' if b['access']==1 else 'read_write')
        typ=structs[b['struct_index']]['name'] if b['kind']==2 else 'array<'+('u32' if b['scalar']==1 else 'f32')+'>'
        resources+=f"@group({b['group']}) @binding({b['binding']}) var<{kind}> {b['name']}: {typ};\n"
    source=(resources+declarations if reorder else declarations+resources)+f'@compute @workgroup_size({n}) fn {entry}(@builtin(global_invocation_id) gid: vec3<u32>) {{ {body} }}'
    return source.encode(),dict(entry=entry,n=[n,1,1],bindings=bindings,structs=structs)

def trial(initial: dict, uniforms: dict, changes: dict, x: int = 0) -> dict:
    want={key:list(value) for key,value in initial.items()}
    for key,items in changes.items():
        for index,value in items.items():
            if 0<=index<len(want[key]):want[key][index]=value&U
    return dict(initial=initial,uniforms=uniforms,want=want,x=x)

def packed(values: list[int]) -> bytes:
    return struct.pack('<'+'I'*len(values),*values)

def admitted(repo: Path) -> list:
    cases=[]
    def add(name: str, structs: list, bindings: list, body: str, vectors: list, **kwargs) -> None:
        source,expected=program(structs,bindings,body,**kwargs)
        expected['vectors']=vectors;cases.append((name,source,expected))
    for count in [1,3,4,5,8]:
        members=[('field_'+str(i),1 if i%2==0 else 2) for i in range(count)]
        values=[(31+i*19) if i%2==0 else [0x3f800001,0xc0200000,0x40800000,0x3e000000][i//2] for i in range(count)]
        shapes=[shape('Record'+str(count),members)]
        bindings=[uniform('record',7,0,count,2),storage('uint_out',1),storage('float_out',4,2)]
        body=' '.join(f"{'uint_out' if t==1 else 'float_out'}[{i}u] = record.{n};" for i,(n,t) in enumerate(members))
        changes={(0,1):{i:v for i,v in enumerate(values) if i%2==0},(0,4):{i:v for i,v in enumerate(values) if i%2}}
        vectors=[trial({(0,1):[S]*(count+2),(0,4):[S]*(count+1)},{(2,7):packed(values)},changes,x) for x in [0,17,U]]
        add('all_members_'+str(count),shapes,bindings,body,vectors,reorder=count in [3,5])
    shapes=[shape('Shared',[('first',1),('second',2),('third',1)])]
    bindings=[uniform('right',9,0,3,3),storage('out',0),uniform('left',2,0,3,1),storage('floats',1,2)]
    raw={(3,9):packed([7,0x40000000,13]),(1,2):packed([3,0x3f000000,5])}
    add('shared_type_resources',shapes,bindings,'out[0u] = right.first + left.third; out[1u] = left.first * right.third; floats[0u] = right.second - left.second;', [trial({(0,0):[S]*3,(0,1):[S]*2},raw,{(0,0):{0:12,1:39},(0,1):{0:0x3fc00000}})],reorder=True)
    shapes=[shape('Dormant',[('padding',1)]),shape('First',[('same',1),('padding',2)]),shape('Second',[('same',2),('padding',1),('tail',1)])]
    bindings=[storage('fout',0,2),uniform('second',4,2,3),uniform('first',2,1,2),uniform('unused',3,0,1)]
    raw={(0,4):packed([0x3fc00000,8,9]),(0,2):packed([7,0x40000000]),(0,3):packed([U])}
    add('scoped_member_names_unused_fields',shapes,bindings,'fout[0u] = first.padding * second.same;', [trial({(0,0):[S]*3},raw,{(0,0):{0:0x40400000}})])
    shapes=[shape('Data',[('start',1),('limit',1),('step',1)])]
    bindings=[uniform('p',3,0,3),storage('a',1,1,1),storage('out',2)]
    for label,body in [
        ('index_base','out[p.start + 1u] = a[p.start - 1u] + p.step;'),
        ('condition_left','if (p.start < p.limit) { out[0u] = p.step; } else { out[1u] = p.start; }'),
        ('condition_right','if (gid.x < p.limit) { out[0u] = p.step; }'),
        ('member_local','let saved = p.step + a[0u]; out[0u] = 7u; out[1u] = saved;'),
        ('member_nonmember_condition','if (p.start >= 1u) { out[0u] = p.step; }'),
        ('offset_condition','if (p.start + 1u > 0u) { out[0u] = p.step; }')]:
        vectors=[]
        for start,limit,step in [(0,1,7),(1,1,9),(2,5,13),(U,U,19)]:
            initial={(0,1):[3],(0,2):[S]*4};changes={}
            if label=='index_base':changes={(start+1)&U:((3 if ((start-1)&U)==0 else 0)+step)&U}
            elif label=='condition_left':changes={0:step} if start<limit else {1:start}
            elif label=='condition_right':changes={0:step} if start<limit else {}
            elif label=='member_local':changes={0:7,1:(step+3)&U}
            elif label=='offset_condition':changes={0:step} if ((start+1)&U)>0 else {}
            else:changes={0:step} if start>=1 else {}
            vectors.append(trial(initial,{(0,3):packed([start,limit,step])},{(0,2):changes},start))
        add(label,shapes,bindings,body,vectors)
    shapes=[shape('Runtime',[('x',2),('unused',1),('y',2),('bound',1),('z',2)])]
    bindings=[uniform('p',0,0,5),storage('out',1,2)]
    add('float_runtime_tree',shapes,bindings,'let v: f32 = p.x * 1.5; out[0u] = max(v - p.y, p.z) + 0.25;', [trial({(0,1):[S]*3},{(0,0):packed([0x40000000,71,0x3f000000,72,0x3f800000])},{(0,1):{0:0x40300000}})])
    shapes=[shape('Only',[('test',1)])]
    add('uniform_only',shapes,[uniform('p',0,0,1)],'if (gid.x < p.test) { return; } return;', [trial({}, {(0,0):packed([1])},{},x) for x in [0,1,U]])
    shapes=[shape('Unused'+str(i),[('a',1)]*1) for i in range(8)]
    add('eight_declared_structs',shapes,[uniform('p',0,7,1)],'return;', [trial({}, {(0,0):packed([11])},{})])
    shapes=[shape('Shared',[('a',1)])]
    bindings=[uniform('p'+str(i),i,0,1) for i in range(8)]
    add('eight_uniform_resources',shapes,bindings,'if (p0.a < p7.a) { return; }', [trial({}, {(0,i):packed([i+1]) for i in range(8)},{})])
    shapes=[shape('Maximal'+str(i), [('member_'+str(j), 1 if j%2==0 else 2) for j in range(8)]) for i in range(8)]
    add('eight_structs_sixtyfour_members',shapes,[uniform('p',31,7,8,3)],'return;', [trial({}, {(3,31):packed([1,0x3f800000]*4)},{})],reorder=True)
    long_name='type_'+('t'*58);member_name='member_'+('m'*56);resource_name='res_'+('r'*59)
    shapes=[shape(long_name,[(member_name,1)])]
    add('maximal_identifiers',shapes,[uniform(resource_name,31,0,1,3),storage('out',0)],f'out[0u] = {resource_name}.{member_name};',[trial({(0,0):[S,S]}, {(3,31):packed([91])},{(0,0):{0:91}})])
    shapes=[shape('Cmp',[('left',1),('right',1)])];bindings=[uniform('p',0,0,2),storage('out',1)]
    operations={'==':lambda x,y:x==y,'!=':lambda x,y:x!=y,'<':lambda x,y:x<y,'<=':lambda x,y:x<=y,'>':lambda x,y:x>y,'>=':lambda x,y:x>=y}
    for index,(op,compare) in enumerate(operations.items()):
        add('member_compare_'+str(index),shapes,bindings,f'if (p.left {op} p.right) {{ out[0u] = 11u; }} else {{ out[0u] = 22u; }}',[trial({(0,1):[S,S]}, {(0,0):packed([left,right])},{(0,1):{0:11 if compare(left,right) else 22}}) for left,right in [(0,0),(0,U),(U,0),(U,U),(1,2)]])
    shapes=[shape('Cfg',[('value',1)])];bindings=[uniform('p',0,0,1),storage('out',1)]
    source,expected=program(shapes,bindings,'out[p.value + ONE] = p.value;')
    source=b'const ONE: u32 = 1u;\n'+source
    expected['vectors']=[trial({(0,1):[S]*4},{(0,0):packed([1])},{(0,1):{2:1}})]
    cases.append(('member_constant_offset',source,expected))
    source,expected=program(shapes,bindings,'out[0u] = p.value;')
    source=source.replace(b'value: u32, }',b'value: u32 }').replace(b'@group(0) @binding(0)',b'@binding(0) @group(0)')
    expected['vectors']=[trial({(0,1):[S]},{(0,0):packed([23])},{(0,1):{0:23}})]
    cases.append(('optional_comma_attribute_reorder',source,expected))
    for typename in ['max','array','vec3u']:
        add('contextual_type_'+typename,[shape(typename,[('f32',1)])],[uniform('p',0,0,1)],'if (p.f32 > 0u) { return; }',[trial({}, {(0,0):packed([1])},{})])
    source,expected=program([shape('u32',[('value',2)])],[uniform('p',0,0,1),storage('out',1,2)],'out[0u] = p.value;')
    source=source.replace(b'gid: vec3<u32>',b'gid: vec3u')
    expected['vectors']=[trial({(0,1):[S]},{(0,0):packed([0x3f800001])},{(0,1):{0:0x3f800001}})]
    cases.append(('contextual_u32_type_unused_builtin',source,expected))
    # Actual unchanged source is not generated and includes four real fields.
    source=(repo/'bench/kernels/package_vector_scale_add.wgsl').read_bytes()
    shapes=[shape('Params',[('count',1),('_pad0',1),('_pad1',1),('_pad2',1)])]
    bindings=[uniform('params',0,0,4),storage('input_values',1,2,1),storage('output_values',2,2)]
    vectors=[]
    for count in [0,1,3,5,U]:
        for inp,outlen in [([],0),([],4),([0x3fc00000],4),([0x3fc00000,0xc0200000,0x3f800001,0x4b000000],3)]:
            for x in [0,1,2,3,4,5,U-1,U]:
                changes={}
                if x<count:
                    value=inp[x] if x<len(inp) else 0
                    changes[x]=operation(129,operation(133,value,0x3fc00000),0x3e800000)
                vectors.append(trial({(0,1):inp,(0,2):[S]*outlen},{(0,0):packed([count,0xdeadbeef,0xcafebabe,0x12345678])},{(0,2):changes},x))
    cases.append(('package_vector_scale_add',source,dict(entry='main',n=[64,1,1],bindings=bindings,structs=shapes,vectors=vectors)))
    return cases

def rejected() -> list[tuple[str,bytes]]:
    shapes=[shape('Cfg',[('a',1),('b',2),('c',1)])]
    bindings=[uniform('p',0,0,3),storage('out',1),storage('fout',2,2)]
    base,_=program(shapes,bindings,'out[0u] = p.a;')
    text=base.decode();cases=[]
    def replace(name: str, old: str, new: str) -> None:
        assert old in text;cases.append((name,text.replace(old,new).encode()))
    for name,body in [
        ('unknown_member','out[0u] = p.missing;'),('uniform_write','p.a = 3u;'),('uniform_index_write','p[0u] = 3u;'),
        ('uniform_aggregate_rhs','out[0u] = p;'),('uniform_aggregate_let','let q = p; out[0u] = q;'),
        ('f32_index','out[p.b] = 3u;'),('f32_condition_left','if (p.b < 1u) { return; }'),('f32_condition_right','if (gid.x < p.b) { return; }'),
        ('mixed_member_rhs','out[0u] = p.a + p.b;'),('mixed_f32_store','fout[0u] = p.a;'),
        ('member_index_offset','out[p.a + p.c] = 3u;'),('runtime_index_offset','let i = 1u; out[p.a + i] = 3u;'),
        ('expression_index','out[(p.a)] = 3u;'),('expression_condition','if (p.a * 2u < 3u) { return; }'),
        ('runtime_nonmember_rhs','let i = 1u; if (p.a < i) { return; }'),
        ('dead_unknown','return; out[0u] = p.unknown;'),('dead_uniform_write','return; p.a = 2u;'),
        ('struct_constructor','let v = Cfg(1u, 2.0, 3u);'),('uniform_atomic','atomicStore(p.a, 1u);'),('member_local_shadow','let p = 1u; out[0u] = p;'),
        ('type_local_shadow','let Cfg = 1u; out[0u] = Cfg;'),('storage_member','out[0u] = out.a;'),
        ('nested_member','out[0u] = p.a.foo;'),('aggregate_assignment','out = p;')]:replace(name,'out[0u] = p.a;',body)
    for name,old,new in [
        ('duplicate_member','c: u32','a: u32'),('duplicate_type','struct Cfg','struct Cfg { z: u32, } struct Cfg'),
        ('unknown_type','p: Cfg','p: Missing'),('scalar_uniform','p: Cfg','p: u32'),('array_uniform','p: Cfg','p: array<u32>'),
        ('nested_struct','a: u32','a: Cfg'),('vector_member','a: u32','a: vec3<u32>'),('array_member','a: u32','a: array<u32>'),
        ('matrix_member','a: u32','a: mat2x2<f32>'),('bool_member','a: u32','a: bool'),('i32_member','a: u32','a: i32'),('f16_member','a: u32','a: f16'),
        ('align_member','a: u32','@align(16) a: u32'),('size_member','a: u32','@size(16) a: u32'),
        ('struct_semicolon','c: u32, }','c: u32, };'),('empty_struct','a: u32, b: f32, c: u32,',''),
        ('duplicate_binding','@binding(1)','@binding(0)'),('duplicate_module_name','var<uniform> p','var<uniform> out'),
        ('uniform_access_annotation','var<uniform>','var<uniform, read>'),('invalid_builtin_type','struct Cfg','struct u32 { x: f32, } struct Cfg')]:replace(name,old,new)
    cases.append(('max_type_shadow_builtin', ('struct max { x: u32, } '+text.replace('out[0u] = p.a;', 'out[0u] = max(p.a, 1u);')).encode()))
    cases.append(('struct_entry_collision', ('struct main { x: u32, } '+text).encode()))
    cases.append(('struct_resource_collision', ('struct p { x: u32, } '+text).encode()))
    cases.append(('ninth_struct',(''.join('struct Extra'+str(i)+' { x: u32, } ' for i in range(8))+text).encode()))
    replace('ninth_member','c: u32,','c: u32, d: u32, e: u32, f: u32, g: u32, h: u32, i: u32,')
    replace('ninth_resource','@compute',''.join(f'@group(3) @binding({i}) var<uniform> other{i}: Cfg; ' for i in range(6))+'@compute')
    replace('name64','a: u32','a'*64+': u32')
    return cases
