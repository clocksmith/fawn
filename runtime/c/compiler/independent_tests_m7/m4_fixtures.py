"""Independently frozen M4 sources and exact expected buffer effects."""
from __future__ import annotations
from fractions import Fraction
import hashlib
from pathlib import Path
import fixtures as retained
from fixtures_m1 import binding
from m2_fixtures import program, vector, A, B, O, S, U
from rational_oracle import decimal_bits, exact_value, operation, round_binary32

LIFTS={
    'wrong_operator':'u32 subtraction is admitted.',
    'extra_expression':'u32 left-associated addition trees are admitted.',
    'f32_literal':'A decimal sibling materializes to runtime f32.',
    'alias_explicit_type':'Annotated u32 scalar local is counted and admitted.',
    'alias_second_let':'Additional scalar lets are counted and admitted.',
    'extra_addition':'Runtime f32 addition trees are admitted.',
    'parenthesized_rhs':'Parenthesized scalar expression is admitted.',
    'constant_alias_shadow_use':'Retained leading alias resolves to invocation x, not same-named module constant.',
    'invalid_statement_10':'Block-scoped u32 let is admitted and usable as index.',
    'invalid_statement_11':'Invocation alias is a runtime u32 scalar value.',
    'invalid_statement_12':'Concrete u32 constant-only subtraction wraps.',
    'dead_after_return_4':'Valid scalar let is validated even after return.',
    'dead_conditional_4':'Valid branch-local scalar let is admitted.',
    'dead_after_both_returns_4':'Valid unreachable scalar let is admitted.'}


def expected_vector(x: int, initial: dict, changes: dict, accesses: list | None = None) -> dict:
    want={key:list(values) for key,values in initial.items()}
    for key,items in changes.items():
        for index,value in items.items():
            if 0 <= index < len(want[key]):want[key][index]=value & U
    result=dict(x=x,initial=initial,want=want)
    if accesses is not None:result['accesses']=accesses
    return result


def admitted(root: Path) -> list:
    cases=[]
    def add(name: str, body: str, vectors: list, **kwargs) -> None:
        source,expect=program(body,**kwargs);expect['vectors']=vectors
        cases.append((name,source,expect))
    init={A:[3,5,9,U],B:[10,20,30,40],O:[S]*5}
    def fixed(value: int) -> list:
        return [expected_vector(x,init,{O:{0:value}}) for x in [0,1,U]]
    arithmetic=[('precedence','3u + 5u * 7u',38),('grouping','(3u + 5u) * 7u',56),
        ('left_subtract','30u - 5u - 7u',18),('right_subtract','30u - (5u - 7u)',32),
        ('u32_wrap','4294967295u * 3u + 4u',1),('u32_max','max(2147483648u, 3u)',0x80000000),
        ('u32_max_reverse','max(3u, 2147483648u)',0x80000000),
        ('u32_max_nested','max(3u + 4u, max(2u, 9u)) * 2u',18)]
    for name,expr,value in arithmetic:add(name,'out[0u] = '+expr+';',fixed(value))
    add('invocation_value','out[0u] = gid.x + idx;',[
        expected_vector(x,init,{O:{0:(x+x)&U}}) for x in [0,1,0x80000001,U]])
    add('load_tree','out[idx] = a[idx] * 3u + b[idx] - 2u;',[
        expected_vector(x,init,{O:{x:((init[A][x] if x<len(init[A]) else 0)*3+(init[B][x] if x<len(init[B]) else 0)-2)&U}})
        for x in [0,1,3,4,5,U]])
    add('local_snapshot','let saved = out[0u]; out[0u] = 7u; out[1u] = saved; out[2u] = out[0u];',[
        expected_vector(0,{A:[],B:[],O:[99,S,S]},{O:{0:7,1:99,2:7}},[('read',O,0),('write',O,0),('write',O,1),('read',O,0),('write',O,2)]),
        expected_vector(U,{A:[],B:[],O:[99]},{O:{0:7}}),expected_vector(U,{A:[],B:[],O:[]},{})])
    add('local_chain','let first: u32 = a[0u] * 2u; let second = first + 3u; out[0u] = max(first, second);',fixed(9))
    add('local_index_condition','let j = gid.x + 1u; if (j > 0u) { out[j - 1u] = a[j] + j; } else { out[0u] = 44u; }',[
        expected_vector(x,init,{O:{0:44}} if x==U else {O:{x:((init[A][x+1] if x+1<len(init[A]) else 0)+x+1)&U}})
        for x in [0,1,3,4,U]])
    add('siblings_reuse','if (idx == 0u) { let value = 11u; out[0u] = value; } else { let value = 22u; out[0u] = value; }',[
        expected_vector(x,init,{O:{0:11 if x==0 else 22}}) for x in [0,1,U]])
    add('branch_dominance_return','let saved = a[0u]; if (idx == 0u) { let inner = saved + 2u; out[0u] = inner; return; } else { let other = saved * 3u; out[1u] = other; } out[2u] = saved;',[
        expected_vector(x,init,{O:{0:5}} if x==0 else {O:{1:9,2:3}}) for x in [0,1,U]])
    add('both_arms_local_return','if (idx == 0u) { let value = a[0u]; out[0u] = value; return; } else { let value = b[0u]; out[0u] = value; return; } let later = a[0u]; out[1u] = later;',[
        expected_vector(x,init,{O:{0:3 if x==0 else 10}}) for x in [0,1,U]])
    add('unreachable_local_valid','return; let later = a[0u] * 2u; out[0u] = later;',[
        expected_vector(0,init,{},[])])
    for name in ['max','u32','f32','array','vec3u']:
        add('contextual_name_'+name,f'let {name} = 3u; out[0u] = {name} * 2u;',fixed(6),alias=False)
    add('contextual_inferred_float','let f32 = 0.1; let u32 = 0.2; out[0u] = f32 + u32;',[
        expected_vector(0,{A:[],B:[],O:[S]},{O:{0:operation(129,decimal_bits('0.1'),decimal_bits('0.2'))}})],scalar=2,alias=False)
    # Decimal expectations are computed directly as exact rational nearest-even, not host floats.
    decimals=['0.0','0.1','0.5','1.0','0.00000001','99999999.9','16777217.0','16777219.0','16777216.9','16777217.1','16777218.9','16777219.1','0.1f','16777217.0f','0000.00000']
    for j,text in enumerate(decimals):
        for mode in ['store','inferred','annotated']:
            body={'store':f'out[0u] = {text};','inferred':f'let v = {text}; out[0u] = v;','annotated':f'let v: f32 = {text}; out[0u] = v;'}[mode]
            add(f'decimal_{j}_{mode}',body,[expected_vector(0,{A:[],B:[],O:[S,S]},{O:{0:decimal_bits(text)}})],scalar=2)
    finit={A:[0x3fc00000,0xc0200000,0x3f800001,0x4b800000],B:[0x40000000,0x3f000000,0x33800000,0x3f800000],O:[S]*5}
    expressions=[('fmul','a[idx] * b[idx]',lambda a,b:operation(133,a,b)),
        ('fsub','a[idx] - b[idx]',lambda a,b:operation(131,a,b)),
        ('fmax','max(a[idx], b[idx])',lambda a,b:a if exact_value(a)>=exact_value(b) else b),
        ('fmax_reverse','max(b[idx], a[idx])',lambda a,b:a if exact_value(a)>=exact_value(b) else b),
        ('float_precedence','a[idx] + b[idx] * 2.0',lambda a,b:operation(129,a,operation(133,b,0x40000000))),
        ('float_grouping','(a[idx] + b[idx]) * 2.0',lambda a,b:operation(133,operation(129,a,b),0x40000000)),
        ('float_left_sub','a[idx] - b[idx] - 1.0',lambda a,b:operation(131,operation(131,a,b),0x3f800000)),
        ('float_no_reassociate','(a[idx] + 1.0) - a[idx]',lambda a,b:operation(131,operation(129,a,0x3f800000),a))]
    for name,expr,formula in expressions:
        vectors=[]
        for initial in [finit,{A:[0x3f800000],B:[],O:[S]*3},{A:[],B:[0x40000000],O:[S]*2},{A:[],B:[],O:[]}]:
            for x in [0,1,3,4,5,U]:
                a=initial[A][x] if x<len(initial[A]) else 0;b=initial[B][x] if x<len(initial[B]) else 0
                vectors.append(expected_vector(x,initial,{O:{x:formula(a,b)}}))
        add(name,'out[idx] = '+expr+';',vectors,scalar=2)
    add('no_fma','out[0u] = a[0u] * b[0u] - 1.0;',[
        expected_vector(0,{A:[0x3f800001],B:[0x3f7ffffe],O:[S]},{O:{0:0}})],scalar=2)
    add('float_local_typed','let value = 0.1; out[0u] = value * 2.0 + 0.2;',[
        expected_vector(0,{A:[],B:[],O:[S]},{O:{0:operation(129,operation(133,decimal_bits('0.1'),0x40000000),decimal_bits('0.2'))}})],scalar=2)
    add('float_snapshot','let saved: f32 = out[0u]; out[0u] = 8.0; out[1u] = saved * 2.0;',[
        expected_vector(0,{A:[],B:[],O:[0x3fc00000,S]},{O:{0:0x41000000,1:0x40400000}})],scalar=2)
    # Boundaries derive mechanically from the frozen node/depth/statement definitions.
    def balanced(n: int) -> str:
        return 'v' if n==1 else '('+balanced(n//2)+' + '+balanced(n-n//2)+')'
    add('nodes_64','let v = 1u; out[0u] = '+balanced(32)+';',fixed(32),alias=False)
    add('depth_binary_16','out[0u] = '+' + '.join(['1u']*16)+';',fixed(16))
    add('depth_parentheses_16','out[0u] = '+'('*15+'1u'+')'*15+';',fixed(1))
    nested='1u'
    for _ in range(15):nested='max(1u, '+nested+')'
    add('depth_max_16','out[0u] = '+nested+';',fixed(1))
    add('statements_16',''.join(f'let q{i} = {i}u;' for i in range(15))+'out[0u] = q14;',fixed(14))
    add('legacy_alias_free','out[0u] = 1u;'*16,fixed(1))
    for variant in ['let idx: u32 = gid.x;','let idx = (gid.x);']:
        add('counted_alias_'+str(len(cases)),variant+'out[0u] = 1u;'*15,fixed(1),alias=False)
    add('four_ifs_locals','if (idx == 0u) {'*4+'let v = 77u; out[0u] = v;'+'}'*4,[expected_vector(x,init,{O:{0:77}} if x==0 else {}) for x in [0,1,U]])
    # Unchanged source families plus identifier/declaration/comment variants.
    for name,path,digest in [('real_scale','bench/kernels/compilation-corpus/simple_scale_f32.wgsl','21f18f6bad8c79bd9246f27b0eb25b714f666b7c8ffbd4f56ae0841589d4cab3'),('real_relu','bench/kernels/compilation-corpus/simple_relu.wgsl','a79c6bb744a3c13f734f1e17a2f9f617c05aac7ae3d64f67c9cc98945f459acc')]:
        source=(root/path).read_bytes();assert hashlib.sha256(source).hexdigest()==digest
        if name=='real_scale':
            bs=[binding('data',0,0,2,2)];vectors=[]
            for values in [[],[0x3fc00000,0xc0200000,0,0x4b800000]]:
                for x in [0,1,3,4,U]:vectors.append(expected_vector(x,{A:values},{A:{x:operation(133,values[x] if x<len(values) else 0,0x40000000)}}))
            expect=dict(entry='main',n=64,bindings=bs,vectors=vectors)
        else:
            bs=[binding('input',0,0,2,1),binding('output',0,1,2,2)];vectors=[]
            for inputs,outputs in [([],[]),([],[S,S]),([0xc0200000,0x3fc00000,0],[S]*4),([0xc0200000,0x3fc00000],[S])]:
                for x in [0,1,2,3,4,U]:
                    value=inputs[x] if x<len(inputs) else 0
                    vectors.append(expected_vector(x,{A:inputs,B:outputs},{B:{x:0 if exact_value(value)<0 else value}}))
            expect=dict(entry='main',n=256,bindings=bs,vectors=vectors)
        cases.append((name,source,expect))
        renamed=source.decode().replace(' main(',' changed(')
        renamed_expect=dict(expect,entry='changed',bindings=[dict(b) for b in bs])
        for old,new in [('data','samples'),('input','origin'),('output','sink_values')]:
            renamed=renamed.replace(old,new)
            for b in renamed_expect['bindings']:
                if b['name']==old:b['name']=new
        renamed=renamed.replace('*','/* lhs */ * /* rhs */').replace('max(','max /*call*/ (')
        if name=='real_relu':
            lines=renamed.splitlines();lines[0],lines[1]=lines[1],lines[0];renamed='\n'.join(lines)
            renamed_expect['bindings'].reverse()
        cases.append((name+'_variant',renamed.encode(),renamed_expect))
    # Former rejection lifts remain exact source and now receive independent behavior.
    old={case[0]:case[1] for case in retained.rejected()}
    for name in LIFTS:
        source=old[name]
        if name in ['wrong_operator','extra_expression']:
            bs=[binding('values',0,0,1,2)];expect=dict(entry='increment',n=64,bindings=bs)
            initial={A:[3,U,0]};delta=-1 if name=='wrong_operator' else 2
            vectors=[expected_vector(x,initial,{A:{x:(initial[A][x]+delta)&U}} if x<3 else {}) for x in [0,1,2,3,U]]
        elif name in ['f32_literal','alias_explicit_type','alias_second_let','extra_addition','parenthesized_rhs']:
            bs=[binding('a',0,0,2,1),binding('b',0,1,2,1),binding('out',0,2,2,2)]
            expect=dict(entry='main',n=256,bindings=bs);initial={A:[0x3f800000],B:[0x40000000],O:[S,S]}
            # Inspect fixture declaration only: extra_addition adds a third input a[idx].
            vectors=[]
            for x in [0,1,U]:
                a=initial[A][x] if x<len(initial[A]) else 0;b=initial[B][x] if x<len(initial[B]) else 0
                val=operation(129,a,0x3f800000) if name=='f32_literal' else operation(129,a,b)
                if name=='extra_addition':val=operation(129,val,a)
                vectors.append(expected_vector(x,initial,{O:{x:val}}))
        elif name=='constant_alias_shadow_use':
            expect=dict(entry='main',n=256,bindings=[binding('a',0,0,1,1),binding('b',0,1,1,1),binding('out',0,2,1,2)])
            vectors=[expected_vector(x,init,{O:{x:((init[A][x] if x<len(init[A]) else 0)+x)&U}}) for x in [0,1,4,U]]
        else:
            _,expect=program('return;')
            vectors=[]
            for x in [0,1,U]:
                changes={}
                if name=='invalid_statement_10' and x==0:changes={O:{0:1}}
                if name=='invalid_statement_11':changes={O:{x:x}}
                if name=='invalid_statement_12':changes={O:{x:U}}
                vectors.append(expected_vector(x,init,changes))
        expect['vectors']=vectors;cases.append(('lift_'+name,source,expect))
    return cases


def rejected() -> list:
    cases=[]
    def bad(name: str,body: str,**kwargs) -> None:
        source,_=program(body,**kwargs);cases.append((name,source,None,None))
    for name,body in {
        'use_before':'out[0u] = later; let later = 1u;',
        'self_init':'let value = value; out[0u] = value;',
        'duplicate':'let value = 1u; let value = 2u; out[0u] = value;',
        'write_local':'let value = 1u; value = 2u;',
        'branch_escape':'if (idx == 0u) { let value = 1u; } out[0u] = value;',
        'sibling_escape':'if (idx == 0u) { let value = 1u; } else { out[0u] = value; }',
        'nested_shadow':'let value = 1u; if (idx == 0u) { let value = 2u; }',
        'alias_shadow':'let idx = 1u;', 'resource_shadow':'let a = 1u;',
        'param_shadow':'let gid = 1u;', 'entry_shadow':'let main = 1u;',
        'local_as_offset':'let value = 1u; out[idx + value] = 1u;',
        'local_as_condition_rhs':'let value = 1u; if (idx == value) { return; }',
        'float_index':'let value = 1.0; out[value] = 1u;',
        'vector_local':'let value = gid; out[0u] = 1u;',
        'max_shadow':'let max = 1u; out[0u] = max(1u, 2u);',
        'u32_shadow_annotation':'let u32 = 1u; let value: u32 = 2u; out[0u] = value;',
        'f32_shadow_annotation':'let f32 = 1u; let value: f32 = 1.0;',
        'unknown_call':'out[0u] = min(1u, 2u);', 'cast':'out[0u] = u32(1u);',
        'max_arity1':'out[0u] = max(1u);','max_arity3':'out[0u] = max(1u,2u,3u);',
        'max_trailing':'out[0u] = max(1u,2u,);','max_empty':'out[0u] = max();',
        'mixed_max':'out[0u] = max(a[0u],1.0);','mixed_arithmetic':'out[0u] = a[0u] * 1.0;',
        'wrong_annotation':'let value: f32 = 1u;',
        'unreachable_bad':'return; let value = missing;',
        'constant_shadow':'let k = 1u;'
    }.items():bad(name,body,constant='const k: u32 = 2u;\n' if name=='constant_shadow' else '')
    for i,expr in enumerate(['0.1 + 0.2','0.1f + 0.2f','1.0 - 0.5','2.0 * 3.0','max(1.0, 2.0)','max(1.0f, 2.0f)','a[idx] * (0.1 + 0.2)','a[idx] * (16777217.0 - 16777216.0)','max(a[idx], max(1.0,2.0))','a[idx] + (1.0f * 2.0f)']):
        bad('constant_float_subtree_'+str(i),'out[idx] = '+expr+';',scalar=2)
    for i,text in enumerate(['.5','5.','1e2','1.0e2','0x1.0p1','1.0h','123456789.0','0.000000001','1.0ff','1.0foo','1.0u','+1.0','-1.0']):
        bad('float_token_'+str(i),'out[idx] = '+text+';',scalar=2)
    for i,expr in enumerate(['a[idx] / 2u','a[idx] % 2u','a[idx] & 2u','a[idx] | 2u','a[idx] ^ 2u','a[idx] << 2u','~a[idx]']):bad('unsupported_op_'+str(i),'out[idx] = '+expr+';')
    def balanced(n: int) -> str:return 'v' if n==1 else '('+balanced(n//2)+' + '+balanced(n-n//2)+')'
    bad('nodes_65','let v = 1u; let w = 1u; out[0u] = '+balanced(32)+';',alias=False)
    bad('depth_binary_17','out[0u] = '+' + '.join(['1u']*17)+';')
    bad('depth_parentheses_17','out[0u] = '+'('*16+'1u'+')'*16+';')
    nested='1u'
    for _ in range(16):nested='max(1u, '+nested+')'
    bad('depth_max_17','out[0u] = '+nested+';')
    bad('statements_17_lets',''.join(f'let q{i} = {i}u;' for i in range(16))+'out[0u] = q15;')
    for i,variant in enumerate(['let idx: u32 = gid.x;','let idx = (gid.x);','let idx = gid.x; let other = gid.x;']):
        bad('counted_alias_over_'+str(i),variant+'out[0u] = 1u;'*16,alias=False)
    bad('depth_if_5','if (idx == 0u) {'*5+'let v = 1u; out[0u] = v;'+'}'*5)
    bad('dead_nodes_65','return; let v = 1u; let w = 1u; out[0u] = '+balanced(32)+';',alias=False)
    return cases
