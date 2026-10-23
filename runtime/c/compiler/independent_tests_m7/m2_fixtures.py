"""M2 black-box cases frozen before reading compiler implementation.

Expected vectors are independent, hand-written outcomes, never parsed from WGSL.
All list values are raw u32 or binary32 bits. No cross-invocation order is assumed.
"""
from __future__ import annotations
import hashlib
import itertools
from pathlib import Path
import fixtures_m1 as m1

LIFTS = {'extra_statement': 'Two assignments now admitted; sequential same-invocation writes.',
         'return_statement': 'A sole return is now an admitted statement.',
         'constant_used_index': 'Module u32 constants now admitted as indices.',
         'index_constant': 'Suffixed u32 literal indices now admitted.',
         'index_arithmetic': 'One u32 addition offset now admitted in indices.'}
S = 0xfeed1234
U = 0xffffffff
A, B, O = (0, 0), (0, 1), (0, 2)


def program(body, scalar=1, constant='', alias=True, bindings=None, entry='main', parameter='gid', n=64):
    if bindings is None:
        bindings = [m1.binding('a', 0, 0, scalar, 1), m1.binding('b', 0, 1, scalar, 1), m1.binding('out', 0, 2, scalar, 2)]
    declarations = [f"@group({v['group']}) @binding({v['binding']}) var<storage, {'read' if v['access']==1 else 'read_write'}> {v['name']}: array<{'u32' if v['scalar']==1 else 'f32'}>;" for v in bindings]
    text = constant + '\n'.join(declarations) + f'\n@compute @workgroup_size({n}) fn {entry}(@builtin(global_invocation_id) {parameter}: vec3u) {{\n'
    text += (f'let idx = {parameter}.x;\n' if alias else '') + body + '\n}\n'
    return text.encode(), dict(entry=entry, n=n, bindings=bindings)


def vector(x, output, a=None, b=None, initial=None):
    initial = {A: [3, 5, 9, U], B: [10, 20, 30, 40], O: [S]*5} if initial is None else initial
    want = {k: list(v) for k, v in initial.items()}
    want[O] = list(output)
    if a is not None: want[A] = list(a)
    if b is not None: want[B] = list(b)
    return dict(x=x, initial=initial, want=want)


def untouched(x, initial):
    return dict(x=x, initial=initial, want={k:list(v) for k,v in initial.items()})


def admitted(root):
    cases=[]
    def add(name, body, vectors, **kw):
        src, exp=program(body, **kw); exp['vectors']=vectors
        cases.append((name,src,exp))
    raw=(root/'bench/fixtures/wgsl-corpus/webgpu/sample-prefix-sum.wgsl').read_bytes()
    assert hashlib.sha256(raw).hexdigest()=='a8a642a54aaca908b707d20666566db8db62dbb5c9bfd518fbe98925da22c416'
    nbind=[m1.binding('input_values',0,0,1,1),m1.binding('output_values',0,1,1,2)]
    nv=[]
    # Hand-authored neighbor results for empty, unequal, wrap, last and first OOB.
    for inp, out, answers in [
        ([], [], {}), ([], [S,S,S], {0:[0,S,S],1:[S,0,S],2:[S,S,0]}),
        ([7], [S,S,S], {0:[7,S,S],1:[S,7,S],2:[S,S,0]}),
        ([3,5,9,U], [S]*5, {0:[3,S,S,S,S],1:[S,8,S,S,S],2:[S,S,14,S,S],3:[S,S,S,8,S],4:[S,S,S,S,U]}),
        ([3,5,9,U], [S], {0:[3]}), ([3,5,9,U], [], {}),
        ([U,2], [S,S], {0:[U,S],1:[S,1]})]:
        for x in sorted({0,1,2,3,4,5,U-1,U,len(inp),len(out)}):
            init={A:inp,B:out}
            nv.append(dict(x=x,initial=init,want={A:list(inp),B:answers.get(x,list(out))}))
    nex=dict(entry='main',n=64,bindings=nbind,vectors=nv)
    cases.append(('real_neighbor_sum',raw,nex))
    for i,order in enumerate(itertools.permutations(range(2))):
        text=raw.decode().replace('input_values','source_'+str(i)).replace('output_values','destination_'+str(i)).replace('index','cursor').replace('gid','position').replace('main','neighbor_'+str(i))
        lines=text.splitlines(); lines[:2]=[lines[j] for j in order]
        exp=dict(nex,entry='neighbor_'+str(i),bindings=[dict(nbind[j], name=('source_' if j==0 else 'destination_')+str(i)) for j in order])
        cases.append(('neighbor_permuted_'+str(i),'\n'.join(lines).encode(),exp))
    # Each comparison has explicit truth answers at equality, below, above and high-bit values.
    truths={'==':[False,True,False,False,False],'!=':[True,False,True,True,True],'<':[True,False,False,False,False],'<=':[True,True,False,False,False],'>':[False,False,True,True,True],'>=':[False,True,True,True,True]}
    for j,(op,answers) in enumerate(truths.items()):
        vs=[vector(x,[11 if take else 22,S,S,S,S]) for x,take in zip([0,1,2,0x80000000,U],answers)]
        add('comparison_'+str(j),f'if (idx {op} 1u) {{ out[0u] = 11u; }} else {{ out[0u] = 22u; }}',vs)
    add('if_without_else','if (idx == 0u) { out[0u] = 11u; } out[1u] = 22u;', [vector(0,[11,22,S,S,S]),vector(1,[S,22,S,S,S]),vector(U,[S,22,S,S,S])])
    add('empty_then','if (idx == 0u) {} else { out[0u] = 22u; } out[1u] = 33u;', [vector(0,[S,33,S,S,S]),vector(1,[22,33,S,S,S])])
    add('empty_else','if (idx == 0u) { out[0u] = 22u; } else {} out[1u] = 33u;', [vector(0,[22,33,S,S,S]),vector(1,[S,33,S,S,S])])
    add('both_empty','if (idx == 0u) {} else {}', [vector(0,[S]*5),vector(U,[S]*5)])
    add('return_only','return;', [vector(0,[S]*5),vector(U,[S]*5)])
    add('unreachable_valid','return; out[0u] = 123u;', [vector(0,[S]*5),vector(U,[S]*5)])
    add('early_then_return','if (idx == 0u) { out[0u] = 11u; return; } out[1u] = 22u;', [vector(0,[11,S,S,S,S]),vector(1,[S,22,S,S,S])])
    add('early_else_return','if (idx == 0u) { out[0u] = 11u; } else { return; } out[1u] = 22u;', [vector(0,[11,22,S,S,S]),vector(1,[S]*5)])
    add('both_arms_return','if (idx == 0u) { out[0u] = 11u; return; } else { out[0u] = 22u; return; } out[1u] = 33u;', [vector(0,[11,S,S,S,S]),vector(1,[22,S,S,S,S])])
    add('oob_store_then_return','if (idx == 0u) { out[4294967295u] = 11u; return; } out[0u] = 22u;', [vector(0,[S]*5),vector(1,[22,S,S,S,S])])
    add('final_return','out[0u] = 11u; return;', [vector(0,[11,S,S,S,S]),vector(U,[11,S,S,S,S])])
    add('nested_four','if (idx < 3u) { if (idx > 0u) { if (idx <= 2u) { if (idx != 1u) { out[0u] = 42u; } else { out[0u] = 41u; } } } } out[1u] = 99u;', [vector(0,[S,99,S,S,S]),vector(1,[41,99,S,S,S]),vector(2,[42,99,S,S,S]),vector(3,[S,99,S,S,S]),vector(U,[S,99,S,S,S])])
    add('nested_return_merge','if (idx < 2u) { if (idx == 0u) { return; } else { out[0u] = 11u; } out[1u] = 22u; } else { out[2u] = 33u; } out[3u] = 44u;', [vector(0,[S]*5),vector(1,[11,22,S,44,S]),vector(2,[S,S,33,44,S])])
    add('sequential_same_binding','out[0u] = 1u; out[1u] = out[0u] + 2u; out[0u] = out[1u] + out[0u]; out[2u] = out[0u];', [vector(0,[4,3,4,S,S]),vector(U,[4,3,4,S,S]),vector(0,[4,3],initial={A:[],B:[],O:[S,S]}),vector(0,[1],initial={A:[],B:[],O:[S]}),vector(0,[],initial={A:[],B:[],O:[]})])
    add('untaken_store_effect','if (idx == 0u) { out[0u] = 7u; } else { out[0u] = 99u; } out[1u] = out[0u] + 1u;', [vector(0,[7,8,S,S,S]),vector(1,[99,100,S,S,S])])
    add('independent_indices','out[idx + 1u] = a[idx - 1u] + b[2u];', [vector(0,[S,30,S,S,S]),vector(1,[S,S,33,S,S]),vector(3,[S,S,S,S,39]),vector(4,[S]*5),vector(U,[30,S,S,S,S]),vector(1,[S,S,0],initial={A:[],B:[],O:[S]*3}),vector(1,[S,S,3],initial={A:[3],B:[],O:[S]*3})])
    add('subtract_destination','out[idx - 1u] = a[idx + 1u];', [vector(0,[S]*5),vector(1,[9,S,S,S,S]),vector(2,[S,U,S,S,S]),vector(3,[S,S,0,S,S]),vector(U,[S]*5)])
    add('literal_index_underflow','out[0u - 1u] = 11u; out[4294967295u + 1u] = 22u;', [vector(0,[22,S,S,S,S]),vector(U,[22,S,S,S,S])])
    add('condition_wrap','if (idx + 1u == 0u) { out[0u] = 11u; } else { out[0u] = 22u; }', [vector(0,[22,S,S,S,S]),vector(U,[11,S,S,S,S])])
    add('condition_underflow','if (idx - 1u > 2u) { out[0u] = 11u; } else { out[0u] = 22u; }', [vector(0,[11,S,S,S,S]),vector(1,[22,S,S,S,S]),vector(3,[22,S,S,S,S]),vector(4,[11,S,S,S,S])])
    add('module_constant_everywhere','if (idx + delta >= delta) { out[delta - 1u] = a[delta] + delta; } else { out[delta] = delta; }', [vector(0,[S,11,S,S,S]),vector(U,[S,S,2,S,S])],constant='const delta: u32 = 2u;\n')
    add('literal_plus_literal','out[idx] = 4294967295u + 2u;', [vector(0,[1,S,S,S,S]),vector(4,[S,S,S,S,1]),vector(5,[S]*5),vector(U,[S]*5)])
    add('single_load_float','out[idx + 1u] = a[idx];', [vector(0,[S,0x80000000,S],initial={A:[0x80000000,0x7fc12345],B:[],O:[S]*3}),vector(1,[S,S,0x7fc12345],initial={A:[0x80000000,0x7fc12345],B:[],O:[S]*3}),vector(U,[0,S,S],initial={A:[],B:[],O:[S]*3})],scalar=2)
    add('conditional_float_add','if (idx == 0u) { out[0u] = a[0u] + b[1u]; } else { out[0u] = a[1u]; }', [vector(0,[0x40400000,S],initial={A:[0x3f800000,0x80000000],B:[0,0x40000000],O:[S,S]}),vector(1,[0x80000000,S],initial={A:[0x3f800000,0x80000000],B:[0,0x40000000],O:[S,S]})],scalar=2)
    add('sixteen_stores','\n'.join('out[0u] = out[0u] + 1u;' for _ in range(16)),[vector(0,[16],initial={A:[],B:[],O:[0]}),vector(U,[],initial={A:[],B:[],O:[]})])
    add('sixteen_returns','return;'*16,[vector(0,[S]*5)])
    add('sixteen_ifs','if (idx == 0u) {} '*16,[vector(0,[S]*5),vector(U,[S]*5)])
    # Eight mixed resources at maximum branch/statement complexity, full reflection retained.
    bs=[m1.binding('r'+str(i),i//2,i%2,1,2) for i in range(8)]
    src,exp=program(' '.join(f'r{i%8}[gid.x + {i}u] = r{(i+1)%8}[gid.x - {i}u] + {i}u;' for i in range(16)),bindings=bs)
    exp['vectors']=[untouched(U,{(i//2,i%2):[] for i in range(8)})]
    cases.append(('max_resources_statements',src,exp))
    for x,value in [(0,11),(1,22)]:
        body='if (idx == 0u) { out[0u] = 11u; } else { out[1u] = 22u; } out[2u] = 33u;'
        vec=vector(x,[11,S,33,S,S] if x==0 else [S,22,33,S,S])
        vec['accesses']=[('write',O,0 if x==0 else 1),('write',O,2)]
        add('branch_access_order_'+str(x),body,[vec])
    vec=vector(0,[S]*5);vec['accesses']=[]
    add('return_has_no_later_accesses','return; out[0u] = a[0u];',[vec])
    vec=vector(0,[11,S,S,S,S]);vec['accesses']=[('write',O,0)]
    add('untaken_load_has_no_effects','if (idx == 0u) { out[0u] = 11u; } else { out[1u] = a[0u]; }',[vec])
    add('operator_adjacent_comments','if (idx /*left*/ <= /*right*/ 1u) { out[0u] = 11u; } else { out[0u] = 22u; }',[vector(1,[11,S,S,S,S]),vector(2,[22,S,S,S,S])])
    old={name:(s,off,cat) for name,s,off,cat in m1.rejected()}
    for name in LIFTS:
        source=old[name][0]
        if name in {'extra_statement','return_statement','constant_used_index'}:
            ex=dict(entry='increment',n=64,bindings=[m1.binding('values',0,0,1,2)])
            init={A:[0,U,2,3,4,5,6,7,8]}
            wants= {'extra_statement': [2,U,2,3,4,5,6,7,8], 'return_statement':init[A], 'constant_used_index':[0,U,2,3,4,5,6,14,8]}
            ex['vectors']=[dict(x=0,initial=init,want={A:wants[name]}),untouched(U,{A:[]})]
        else:
            _,ex=m1.residual()
            init={A:[0x3f800000,0x40000000],B:[0x40000000,0x40400000],O:[S,S,S]}
            if name=='index_constant':
                ex['vectors']=[dict(x=x,initial=init,want={A:init[A],B:init[B],O:[0x40400000,S,S]}) for x in [0,U]]
            else:
                ex['vectors']=[dict(x=0,initial=init,want={A:init[A],B:init[B],O:[S,0x40a00000,S]}),dict(x=U,initial=init,want={A:init[A],B:init[B],O:[0x40400000,S,S]})]
        cases.append(('lift_'+name,source,ex))
    return cases


def rejected():
    cases=[]
    def bad(name,body,**kw):
        source,_=program(body,**kw);cases.append((name,source,None,None))
    for i,cond in enumerate(['idx = = 1u','idx ! /*gap*/ = 1u','idx < = 1u','idx >\n= 1u','idx = 1u','idx === 1u','idx == 1','idx == 1.0','idx == gid.x','idx == missing','idx == 1u && idx == 0u','idx == 1u || idx == 0u','a[idx] == 0u','idx + 1u + 1u == 0u','(idx) == 0u','gid.y == 0u','gid.z == 0u','idx + 1.0 == 0u','idx - idx == 0u']):
        bad('invalid_condition_'+str(i),f'if ({cond}) {{ out[0u] = 1u; }}')
    for i,body in enumerate(['if idx == 0u { out[0u] = 1u; }','if (idx == 0u) out[0u] = 1u;','if (idx == 0u) {} else if (idx == 1u) {}','if (idx == 0u) {} else out[0u] = 1u;','else {}','{}','return 1u;','return','while (idx == 0u) {}','loop {}','if (idx == 0u) { let j = gid.x; out[j] = 1u; }','out[idx] = idx;','out[idx] = 1u - 2u;','out[(idx)] = 1u;','out[idx + 1u + 2u] = 1u;','out[idx - 1u - 2u] = 1u;','out[idx * 1u] = 1u;','out[1.0] = 1u;','out[-1u] = 1u;','out[missing] = 1u;','out[4294967296u] = 1u;','out[gid.y] = 1u;','out[idx + gid.x] = 1u;','out[idx + idx] = 1u;']):
        bad('invalid_statement_'+str(i),body)
    for i,invalid in enumerate(['missing[0u] = 1u;', 'a[0u] = 1u;', 'out[0u] = 1.0;', 'out[0u] = a[missing];', 'let hidden = gid.x;', 'out[0u] = missing;', 'out[0u] = b[0u];']):
        kw={}
        if i==6:
            kw['bindings']=[m1.binding('a',0,0,1,1),m1.binding('b',0,1,2,1),m1.binding('out',0,2,1,2)]
        for prefix,suffix,label in [('return; ','','after_return'),('if (idx == 4294967295u) { ',' }','conditional'),('if (idx == 0u) { return; } else { return; } ','','after_both_returns')]:
            bad('dead_'+label+'_'+str(i),prefix+invalid+suffix,**kw)
    bad('condition_shadow_alias','if (idx == idx) { return; }',constant='const idx: u32 = 1u;\n')
    bad('index_offset_shadow_alias','out[0u + idx] = 1u;',constant='const idx: u32 = 1u;\n')
    bad('condition_shadow_parameter','if (idx == gid) { return; }',constant='const gid: u32 = 1u;\n')
    for name,body in [('statement_17','out[0u] = 1u; '*17),('unreachable_statement_17','return; '*17),('empty_if_17','if (idx == 0u) {} '*17),('nested_if_5','if (idx == 0u) {'*5+'return;'+'}'*5)]:
        source,_=program(body)
        token=b'if' if name in {'empty_if_17','nested_if_5'} else b'return' if name=='unreachable_statement_17' else b'out['
        positions=[];start=0
        while True:
            off=source.find(token,start)
            if off<0:break
            positions.append(off);start=off+len(token)
        offending=positions[4 if name=='nested_if_5' else 16]
        cases.append((name,source,offending,1))
    return cases
