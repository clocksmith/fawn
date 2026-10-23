"""Independent contract-frozen M6 source programs and mathematical expected effects."""
from __future__ import annotations
from pathlib import Path
from fixtures_m5 import storage, uniform, shape, declaration, packed

U = 0xffffffff
S = 0xa55a36c9
BUILTINS = {24: 'num_workgroups', 26: 'workgroup_id', 27: 'local_invocation_id',
            28: 'global_invocation_id', 29: 'local_invocation_index'}
DEFAULT = [(28, 'gid', 'vec3u')]

def program(body: str, bindings: list | None = None, structs: list | None = None,
            parameters: list | None = None, size: str = '1',
            dimensions: tuple = (1, 1, 1), entry: str = 'main') -> tuple:
    bindings = [] if bindings is None else bindings
    structs = [] if structs is None else structs
    parameters = DEFAULT if parameters is None else parameters
    text = ''.join(declaration(s) for s in structs)
    for b in bindings:
        kind = 'uniform' if b['kind'] == 2 else 'storage, ' + ('read' if b['access'] == 1 else 'read_write')
        typ = structs[b['struct_index']]['name'] if b['kind'] == 2 else 'array<' + ('u32' if b['scalar'] == 1 else 'f32') + '>'
        text += f"@group({b['group']}) @binding({b['binding']}) var<{kind}> {b['name']}: {typ};\n"
    args = ', '.join(f'@builtin({BUILTINS[b]}) {name}: {typ}' for b, name, typ in parameters)
    text += f'@compute @workgroup_size({size}) fn {entry}({args}) {{ {body} }}'
    return text.encode(), dict(entry=entry, n=list(dimensions), bindings=bindings,
                              structs=structs, builtins=[b for b, _, _ in parameters])

def invocation(local: tuple = (0, 0, 0), group: tuple = (0, 0, 0),
               size: tuple = (1, 1, 1), groups: tuple = (1, 1, 1)) -> dict:
    assert all(0 <= local[i] < size[i] and 0 <= group[i] < groups[i] for i in range(3))
    return {24: groups, 26: group, 27: local,
            28: tuple(group[i] * size[i] + local[i] for i in range(3)),
            29: local[0] + size[0] * (local[1] + size[1] * local[2])}

def trial(initial: dict, changes: dict, inputs: dict | None = None,
          uniforms: dict | None = None, synthetic: bool = False) -> dict:
    want = {k: list(v) for k, v in initial.items()}
    for key, values in changes.items():
        for index, value in values.items():
            if 0 <= index < len(want[key]):
                want[key][index] = value & U
    return dict(initial=initial, want=want, uniforms=uniforms or {},
                inputs=inputs or invocation(), synthetic=synthetic)

def load(values: list, index: int) -> int:
    return values[index] if 0 <= index < len(values) else 0

def positives(repo: Path) -> list:
    cases = []
    def add(name: str, body: str, vectors: list, **kwargs) -> None:
        source, expected = program(body, **kwargs)
        expected['vectors'] = vectors
        cases.append((name, source, expected))

    # Unequal buffers and every access's independent post-wrap guard.
    bindings = [storage('tokens', 0, access=1), storage('values', 1, access=1), storage('out', 2)]
    vectors = []
    for tokens in [[], [U], [0x80000000], [0, 2, U, 0x80000000]]:
        for values in [[], [11], [11, 23, 37, 41, 59, 61]]:
            for length in [0, 1, 3, 8]:
                for x in [0, 1, 3, 4, U]:
                    token = load(tokens, x)
                    index = (token + 1) & U
                    rhs = (load(values, (token * 2 + 1) & U) + 7) & U
                    vectors.append(trial({(0,0):tokens, (0,1):values, (0,2):[S]*length},
                                         {(0,2):{index:rhs}}, {28:(x,0,0)}, synthetic=True))
    add('indirect_wrap_each_binding', 'out[tokens[gid.x] + 1u] = values[tokens[gid.x] * 2u + 1u] + 7u;', vectors, bindings=bindings)
    vectors = []
    for tokens in [[], [0], [1, 3], [U, 0, 1, 2]]:
        for values in [[], [77], [11, 23, 37, 41]]:
            for x in [0, 1, 2, U]:
                index = load(tokens, load(tokens, x))
                vectors.append(trial({(0,0):tokens, (0,1):values, (0,2):[S]*5},
                                     {(0,2):{index:load(values,index)}}, {28:(x,0,0)}, synthetic=True))
    add('nested_indirect_oob_zero', 'out[tokens[tokens[gid.x]]] = values[tokens[tokens[gid.x]]];', vectors, bindings=bindings)

    compare = {'==':lambda a,b:a==b, '!=':lambda a,b:a!=b, '<':lambda a,b:a<b,
               '<=':lambda a,b:a<=b, '>':lambda a,b:a>b, '>=':lambda a,b:a>=b}
    for op, fn in compare.items():
        vectors = []
        for tokens in [[], [1], [U, 0], [0, 1, 2]]:
            for values in [[], [9], [2, 7, 0]]:
                for x in [0, 1, 3, U]:
                    left = (load(values, load(tokens,x)) + 3) & U
                    right = max(load(tokens,load(values,x)), 1)
                    vectors.append(trial({(0,0):tokens,(0,1):values,(0,2):[S]*3},
                                         {(0,2):{0:101 if fn(left,right) else 202}}, {28:(x,0,0)}, synthetic=True))
        add('condition_nested_' + str(len(cases)), f'if (values[tokens[gid.x]] + 3u {op} max(tokens[values[gid.x]], 1u)) {{ out[0u] = 101u; }} else {{ out[0u] = 202u; }}', vectors, bindings=bindings)
    initial = {(0,0):[], (0,1):[5,9], (0,2):[S]*4}
    add('nested_selection_phi_predecessor', 'if (values[tokens[gid.x]] == 5u) { if (tokens[values[gid.x]] < 1u) { out[0u] = values[tokens[gid.x]]; } else { out[1u] = 2u; } } else { out[2u] = 3u; }',
        [trial(initial,{(0,2):{0:5}})], bindings=bindings)
    add('snapshot_source_order', 'let before = out[0u]; out[0u] = 2u; out[before] = out[out[0u]]; out[3u] = before;',
        [trial({(0,0):[1,7,99,S,S]}, {(0,0):{0:2,1:99,3:1}})], bindings=[storage('out',0)], parameters=[])
    structs = [shape('Cfg',[('start',1),('offset',1)])]
    bindings = [uniform('p',0,0,2),storage('src',1,access=1),storage('out',2)]
    vectors=[]
    for start, offset in [(0,0),(U,1),(0x80000000,1),(2,1)]:
        index=(start*2+offset)&U
        vectors.append(trial({(0,1):[19,31,43],(0,2):[S]*5}, {(0,2):{index:load([19,31,43],index)}},
                             uniforms={(0,0):packed([start,offset])}))
    add('uniform_indices_tree', 'out[p.start * 2u + p.offset] = src[max(p.start * 2u + p.offset, 0u)];', vectors,
        bindings=bindings, structs=structs, parameters=[])

    # Semantic builtin identities: varied names, declaration order, all thirteen scalars.
    params=[(29,'slot','u32'),(26,'tile','vec3<u32>'),(24,'counts','vec3u'),(28,'point','vec3u'),(27,'lane','vec3<u32>')]
    size=(4,2,2); group=(1,2,3); groups=(3,4,5)
    expressions=[(b, f'{name}.{axis}', i) for b,name,_ in params if b!=29 for i,axis in enumerate('xyz')]+[(29,'slot',None)]
    vectors=[]
    initial={(0,0):[S]*(13*16+3)}
    dispatch=[];changes={}
    for z in range(2):
        for y in range(2):
            for x in range(4):
                inputs=invocation((x,y,z),group,size,groups);dispatch.append(inputs)
                for i,(b,_,component) in enumerate(expressions):
                    changes[i*16+inputs[29]]=inputs[b] if component is None else inputs[b][component]
    v=trial(initial,{(0,0):changes});v['dispatch']=dispatch
    body=' '.join(f'out[{i}u * 16u + slot] = {expr};' for i,(_,expr,_) in enumerate(expressions))
    add('all_builtin_components_coherent_dispatch',body,[v],bindings=[storage('out',0)],parameters=params,size='4, 2, 2,',dimensions=size)
    for b,name,typ in params:
        expr=name if b==29 else name+'.z'
        inputs=invocation((2,1,1),group,size,groups)
        value=inputs[b] if b==29 else inputs[b][2]
        add('single_builtin_'+str(b),f'out[0u] = {expr};',[trial({(0,0):[S,S]}, {(0,0):{0:value}},inputs)],
            bindings=[storage('out',0)],parameters=[(b,name,typ)],size='4,2,2',dimensions=size)
    add('long_entry_five_unused_inputs','', [trial({}, {})],parameters=params,entry='e'*63)
    source,exp=program('let chosen = point.x; out[chosen] = tile.y;',bindings=[storage('out',0)],parameters=params)
    exp['vectors']=[trial({(0,0):[S]*3},{(0,0):{1:2}}, {28:(1,0,0),26:(0,2,0),24:(1,1,1),27:(0,0,0),29:0},synthetic=True)]
    cases.append(('legacy_alias_by_semantic_identity',source,exp))
    for text,dims in [('1',(1,1,1)),('256,',(256,1,1)),('8, 8',(8,8,1)),('4,4,16',(4,4,16)),('1,1,256,',(1,1,256))]:
        add('dimensions_'+text.replace(',','_').replace(' ',''),'',[trial({}, {})],parameters=[],size=text,dimensions=dims)
    add('no_resources_nonempty','let one = 1u; if (one == 1u) { return; }',[trial({}, {})],parameters=[])
    add('unused_resource_zero_inputs','',[trial({(0,0):[S]*3},{})],bindings=[storage('out',0)],parameters=[])
    # Parameter shadowing is legal; scalar builtin must win over module resource.
    add('parameter_shadows_resource','other[0u] = out;',[trial({(0,0):[S],(0,1):[S]}, {(0,1):{0:0}})],
        bindings=[storage('out',0),storage('other',1)],parameters=[(29,'out','u32')])

    # Pure scalar locals avoid emission-cap masking the exact parser node boundary.
    def tree(n: int) -> str:
        if n==1:return '1u'
        left=n//2
        return f'max({tree(left)}, {tree(n-left)})'
    add('nodes_320','let a = '+tree(160)+'; let b = a;', [trial({}, {})],parameters=[])
    add('depth_18_parentheses','let a = '+'('*17+'1u'+')'*17+';', [trial({}, {})],parameters=[])
    add('depth_18_nested_loads','let a = '+'out['*17+'0u'+']'*17+';',
        [trial({(0,0):[0,S]}, {})],bindings=[storage('out',0)],parameters=[])
    add('statements_16',' '.join(f'let a{i} = {i}u;' for i in range(16)),[trial({}, {})],parameters=[])
    add('if_depth_4','if (1u == 1u) {'*4+'return;'+'}'*4,[trial({}, {})],parameters=[])

    # Retained M5 accounting can reach beyond the old RHS-only capacities.
    add('old_depth16_with_index_edges', 'out[0u] = out[gid.x + 1u]' + ' + 1u'*15 + ';',
        [trial({(0,0):[17,29,41]}, {(0,0):{0:44}})], bindings=[storage('out',0)])
    def old_tree(n: int) -> str:
        if n==1:return 'out[gid.x + 1u]'
        return f'max({old_tree(n//2)}, {old_tree(n-n//2)})'
    add('old_64_rhs_nodes_plus_indices', 'let a = 1u; out[0u] = '+old_tree(32)+';',
        [trial({(0,0):[17,29,41]}, {(0,0):{0:29}})], bindings=[storage('out',0)])

    # Exact unchanged real witnesses with independently authored resource layouts.
    structs=[shape('Uniforms',[('seq_len',1),('embed_dim',1),('_pad0',1),('_pad1',1)])]
    bindings=[uniform('u',0,0,4),storage('token_ids',1,access=1),storage('embed_table',2,2,1),storage('output',3,2)]
    _,expected=program('',structs=structs,bindings=bindings,size='256',dimensions=(256,1,1))
    expected['vectors']=[]
    for tokens in [[],[U],[0x80000000],[1,0,U]]:
        for table in [[],[0x3f800001],[0x3f800001,0x40000000,0x40400000,0x40800000,0x40a00000]]:
            for outlen in [0,1,5,9]:
                dim=2;seq=3;initial={(0,1):tokens,(0,2):table,(0,3):[S]*outlen};changes={};dispatch=[]
                for y in range(3):
                    for x in range(4):
                        dispatch.append(invocation((x,0,0),(0,y,0),(256,1,1),(1,3,1)))
                        if x<dim:
                            token=load(tokens,y);changes[y*dim+x]=load(table,(token*dim+x)&U)
                v=trial(initial,{(0,3):changes},uniforms={(0,0):packed([seq,dim,71,99])});v['dispatch']=dispatch
                expected['vectors'].append(v)
    cases.append(('real_gather',(repo/'bench/inference-pipeline/kernels/gather.wgsl').read_bytes(),expected))
    _,expected=program('',bindings=[storage('input',0,2,1),storage('output',1,2)],parameters=[(26,'wid','vec3u')])
    expected['vectors']=[]
    for inp in [[],[0x3f800001],[0x80000000,0x7fc01234,0x3f800001]]:
        for n in [0,1,5]:
            v=trial({(0,0):inp,(0,1):[S]*n},{(0,1):{i:load(inp,i) for i in range(7)}})
            v['dispatch']=[invocation(group=(i,0,0),groups=(7,1,1)) for i in range(7)]
            expected['vectors'].append(v)
    cases.append(('real_copy_buffer',(repo/'bench/inference-pipeline/kernels/copy_buffer.wgsl').read_bytes(),expected))
    for name in ['compilation-corpus/trivial_noop_compute','copy_textures_x32','dispatch_noop','draw_call_proxy_noop']:
        source=(repo/'bench/kernels'/f'{name}.wgsl').read_bytes()
        # Actual no-op sources have no parameters/resources; texture-named witness has x=64.
        n = 64 if name == 'copy_textures_x32' else 1
        _,expected=program('',parameters=[],size=str(n),dimensions=(n,1,1));expected['vectors']=[trial({}, {})]
        cases.append(('real_'+name.split('/')[-1],source,expected))
    return cases


def negatives() -> list:
    cases=[]
    bindings=[storage('out',0),storage('floats',1,2,1)]
    for name,body in [
        ('f32_index','out[1.0] = 1u;'),('f32_indirect_index','out[floats[0u]] = 1u;'),
        ('f32_compare_left','if (floats[0u] < 1u) { return; }'),
        ('f32_compare_right','if (1u < floats[0u]) { return; }'),
        ('comparison_chain','if (1u < 2u < 3u) { return; }'),
        ('comparison_as_value','let v = 1u < 2u;'),('bool_condition','if (1u) { return; }'),
        ('logical_condition','if (1u < 2u && 2u < 3u) { return; }'),
        ('vector_as_scalar','out[0u] = gid;'),('vector_let','let v = gid;'),
        ('vector_index','out[gid] = 1u;'),('component_w','out[gid.w] = 1u;'),
        ('component_chain','out[gid.x.x] = 1u;'),('dead_type_error','return; out[floats[0u]] = 1u;'),
        ('local_shadow','let gid = 1u;'),('unsupported_division','out[0u] = gid.x / 2u;'),
        ('missing_index_close','out[out[0u] = 1u;')]:
        cases.append((name,program(body,bindings=bindings)[0]))
    for name,parameters in [
        ('duplicate_builtin',[(28,'a','vec3u'),(28,'b','vec3u')]),
        ('duplicate_parameter',[(28,'a','vec3u'),(27,'a','vec3u')]),
        ('global_scalar',[(28,'a','u32')]),('index_vector',[(29,'a','vec3u')]),
        ('global_float_vector',[(28,'a','vec3<f32>')]),('index_float',[(29,'a','f32')])]:
        cases.append((name,program('',parameters=parameters)[0]))
    cases.append(('component_on_scalar',program('out[0u] = n.x;',bindings=bindings,parameters=[(29,'n','u32')])[0]))
    for value in ['0','257','1,0','1,1,257','17,16','256,256,256','4294967295,4294967295,4294967295','1,1,1,1','1,,1']:
        cases.append(('bad_dimensions_'+value,program('',parameters=[],size=value)[0]))
    for name,old,new in [('unsupported_builtin','global_invocation_id','position'),
                         ('unsupported_attribute','@builtin','@location'),
                         ('missing_builtin','@builtin(global_invocation_id) ',''),
                         ('double_builtin','@builtin(global_invocation_id)','@builtin(global_invocation_id) @builtin(local_invocation_id)'),
                         ('entry_name64','fn main','fn '+'e'*64)]:
        cases.append((name,program('')[0].replace(old.encode(),new.encode())))
    def tree(n: int) -> str:
        if n==1:return '1u'
        return f'max({tree(n//2)}, {tree(n-n//2)})'
    cases.append(('nodes_321',program('let a = '+tree(161)+';',parameters=[])[0]))
    cases.append(('depth_19_parentheses',program('let a = '+'('*18+'1u'+')'*18+';',parameters=[])[0]))
    cases.append(('depth_19_nested_loads',program('let a = '+'out['*18+'0u'+']'*18+';',bindings=[storage('out',0)],parameters=[])[0]))
    cases.append(('statements_17',program(' '.join(f'let a{i} = {i}u;' for i in range(17)),parameters=[])[0]))
    cases.append(('if_depth_5',program('if (1u == 1u) {'*5+'return;'+'}'*5,parameters=[])[0]))
    return cases
