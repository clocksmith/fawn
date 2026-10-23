"""Independent M7 contract fixtures and expected resource effects.

The expected values are direct arithmetic/state-machine outcomes written before
candidate inspection. No emitted program or compiler IR computes expectations.
"""
from __future__ import annotations

import math
from fixtures_m5 import storage
from fixtures_m6 import program, trial, U, S
from typed_oracle import bits_f32


def positives() -> list:
    cases = []
    def add(name: str, body: str, outputs: list[int] | None = None,
            vectors: list | None = None, bindings: list | None = None,
            loops: bool = False, mutable: bool = False,
            unfinished: bool = False) -> None:
        bindings = [storage('out', 0)] if bindings is None else bindings
        source, expected = program(body, bindings=bindings)
        if vectors is None:
            values = [] if outputs is None else outputs
            vectors = [trial({(0, 0): [S] * (len(values) + 2)},
                             {(0, 0): dict(enumerate(values))})]
        expected.update(vectors=vectors, require_loop=loops,
                        require_function_memory=mutable, unfinished=unfinished)
        cases.append((name, source, expected))

    add('mutable_snapshot', 'var x:u32=7u; let old=x; x=11u; out[0]=old; out[1]=x; x+=3u; out[2]=x; x-=2u; out[3]=x; x*=4u; out[4]=x; x++; out[5]=x; x--; out[6]=x;', [7,11,14,12,48,49,48], mutable=True)
    add('zero_scalar_types', 'var x:u32; var y:bool; var z:f32; out[0]=x; if y {out[1]=9u;} else {out[1]=2u;} if z==0.0 {out[2]=3u;}', [0,2,3], mutable=True)
    add('function_address_space', 'var<function> x:u32=4; x*=3u; out[0]=x;', [12], mutable=True)
    add('declaration_zero_each_iteration', 'for(var i:u32=0u;i<4u;i++){var x:u32; out[i]=x; x=99u;}', [0,0,0,0], loops=True, mutable=True)
    add('declaration_snapshot_each_iteration', 'for(var i=0u;i<4u;i++){let saved=i; i+=1u; out[saved]=saved; i-=1u;}', [0,1,2,3], loops=True, mutable=True)
    add('inner_scope_shadowing', 'var x=3u; {let x=x+2u; out[0]=x; {var x=8u; x++; out[1]=x;} out[2]=x;} out[3]=x;', [5,9,5,3], mutable=True)
    add('sibling_scope_reuse', '{var x=4u; out[0]=x;} {let x=7u;out[1]=x;}', [4,7], mutable=True)
    add('parameter_resource_shadowing', '{let gid=9u;out[0]=gid;} {let out=6u; var x=out; x++;} out[1]=gid.x;', [9,0], mutable=True)
    add('annotation_resolves_before_new_name', 'var u32:u32=6u; out[0]=u32;', [6], mutable=True)
    add('builtin_resolves_before_new_name', 'let max=max(2u,3u);out[0]=max;', [3])
    add('separate_for_scopes', 'for(var i=0u;i<2u;i++){out[i]=i+4u;} for(var i=0u;i<2u;i++){out[i+2u]=i+8u;}', [4,5,8,9], loops=True, mutable=True)
    add('for_zero_one_many', 'var sum=0u;for(var i=0u;i<gid.x;i++){sum+=i+1u;}out[0]=sum;', vectors=[trial({(0,0):[S,S]}, {(0,0):{0:n*(n+1)//2}}, {28:(n,0,0)},synthetic=True) for n in [0,1,7,1234]], loops=True, mutable=True)
    add('for_omitted_clauses', 'var n=0u;for(;;){n++;if n==4u {break;}}out[0]=n;', [4], loops=True, mutable=True)
    add('for_assignment_init_and_update', 'var n=9u;var s=0u;for(n=1u;n<4u;n+=1u){s+=n;}out[0]=s;out[1]=n;', [6,4], loops=True, mutable=True)
    add('for_continue_updates', 'var sum=0u;var i=0u;for(;i<5u;i++){if i<2u {continue;} sum+=i;}out[0]=sum;out[1]=i;', [9,5], loops=True, mutable=True)
    add('for_break_skips_update', 'var i=0u;for(;i<8u;i++){if i==3u {break;}}out[0]=i;', [3], loops=True, mutable=True)
    add('for_return_skips_update', 'var i=0u;for(;i<5u;out[1]++){out[0]=i;return;}', vectors=[trial({(0,0):[S,4,S]}, {(0,0):{0:0}})], loops=True, mutable=True)
    add('while_postguard_test', 'var i=0u;while a[i]>0u {out[i]=a[i]+i;i++;}', bindings=[storage('out',0),storage('a',1,access=1)], vectors=[trial({(0,0):[S]*5,(0,1):v},{(0,0):{i:x+i for i,x in enumerate(v[:next((j for j,x in enumerate(v) if not x),len(v))])}}) for v in [[],[0],[2],[3,4,0,9]]], loops=True, mutable=True)
    add('nested_nearest_break_continue', 'var sum=0u;for(var i=0u;i<3u;i++){for(var j=0u;j<5u;j++){if j==1u {continue;}if j==3u {break;}sum+=i+j;}}out[0]=sum;', [12], loops=True, mutable=True)
    add('loop_continuing_break_if', 'var i=0u;loop {out[i]=i+10u; continuing {i++;break if i>=4u;}}', [10,11,12,13], loops=True, mutable=True)
    add('continuing_body_local', 'var i=0u;loop {let saved=i+3u;continuing {out[i]=saved;i++;break if i==3u;}}', [3,4,5], loops=True, mutable=True)
    add('continue_after_declaration', 'var i=0u;loop {let saved=i+7u;if i<2u {continue;}continuing {out[i]=saved;i++;break if i==3u;}}', [7,8,9], loops=True, mutable=True)
    add('continuing_nested_loop', 'var i=0u;loop {out[i]=i;continuing {var j=0u;loop {j++;if j<2u {continue;}break;}i+=j;break if i>=4u;}}', vectors=[trial({(0,0):[S]*5}, {(0,0):{0:0,2:2}})], loops=True, mutable=True)
    add('return_bypasses_continuing', 'loop {out[0]=4u;return;continuing {out[0]=9u;break if true;}}', [4], loops=True)
    add('both_arm_loop_exit', 'var n=0u;loop {if gid.x==0u {n=7u;break;}else {n=9u;break;}}out[0]=n;', vectors=[trial({(0,0):[S,S]}, {(0,0):{0:7 if x==0 else 9}}, {28:(x,0,0)},synthetic=True) for x in [0,1]], loops=True, mutable=True)
    add('dead_valid_source_not_emitted', 'out[0]=3u;return;var x=8u;while x>0u {x--;out[0]=x;}', [3])
    add('empty_statements_and_blocks', ';{};{var x=3u;{};out[0]=x;};;', [3], mutable=True)
    add('bool_snapshots_and_not', 'var x=true;let old=x;x=!x;if old {out[0]=7u;}if !x {out[1]=9u;}if x==false {out[2]=11u;}if old!=x {out[3]=13u;}', [7,9,11,13], mutable=True)
    add('else_if_chain', 'if gid.x==0u {out[0]=1u;}else if gid.x==1u {out[0]=2u;}else if gid.x==2u {out[0]=3u;}else{out[0]=4u;}', vectors=[trial({(0,0):[S,S]}, {(0,0):{0:min(x+1,4)}}, {28:(x,0,0)},synthetic=True) for x in [0,1,2,7]])
    for label,condition,first,expected_value in [('and_false','false && a[0]>0u',False,2),('or_true','true || a[0]>0u',True,1),('and_true','true && a[0]>0u',True,1),('or_false','false || a[0]>0u',False,1)]:
        vector=trial({(0,0):[S,S],(0,1):[7]}, {(0,0):{0:expected_value}})
        vector['accesses']=([] if label in {'and_false','or_true'} else [('read',(0,1),0)])+[('write',(0,0),0)]
        add('short_circuit_'+label, f'if {condition} {{out[0]=1u;}}else{{out[0]=2u;}}', bindings=[storage('out',0),storage('a',1,access=1)], vectors=[vector])
    add('nested_logical_postguard_phi', 'if (a[a[1]]==7u || a[a[2]]==9u) && (a[a[3]]==0u || false) {out[0]=11u;}else{out[0]=22u;}', bindings=[storage('out',0),storage('a',1,access=1)], vectors=[trial({(0,0):[S,S],(0,1):v},{(0,0):{0:11 if v else 22}}) for v in [[7,0,U,U],[]]])
    add('short_circuit_loop_test', 'var i=0u;while i<3u && a[i]>0u {out[i]=i+1u;i++;}', bindings=[storage('out',0),storage('a',1,access=1)], vectors=[trial({(0,0):[S]*5,(0,1):[3,4,5,6]},{(0,0):{0:1,1:2,2:3}})], loops=True, mutable=True)
    # The target index is observed before the value; compounds read target once.
    for compound,body,want,trace in [(False,'out[out[0]]=out[1];',{2:17},[('read',(0,0),0),('read',(0,0),1),('write',(0,0),2)]),(True,'out[out[0]]+=out[1];',{2:22},[('read',(0,0),0),('read',(0,0),2),('read',(0,0),1),('write',(0,0),2)])]:
        vector=trial({(0,0):[2,17,5,S]},{(0,0):want});vector['accesses']=trace
        add('compound_target_order' if compound else 'simple_target_order',body,vectors=[vector])
    vectors=[]
    for a in [[],[U],[0,2,U]]:
        for length in [0,1,4]:
            initial={(0,0):[5]*length,(0,1):a};index=((a[0] if a else 0)+1)&U
            vectors.append(trial(initial,{(0,0):{index:((5 if index<length else 0)+(a[1] if len(a)>1 else 0))&U}}))
    add('compound_independent_robustness','out[a[0]+1u]+=a[1];',bindings=[storage('out',0),storage('a',1,access=1)],vectors=vectors)
    add('concrete_constant_wrap', 'out[0]=0u-1u;out[1]=4294967295u+1u;var n=4294967295u;n++;out[2]=n;', [U,0,0],mutable=True)
    add('contextual_direct_literals', 'var x:u32=1;var f:f32=1;x=x+2;out[0]=x;out[1]=max(1,2u);if 1<2u {out[2]=3u;}if f==1 {out[3]=4u;}', [3,2,3,4],mutable=True)
    add('direct_abstract_comparisons', 'if 4294967295>2147483648 {out[0]=1u;}if 1==1 {out[1]=2u;}if 0!=1 {out[2]=3u;}', [1,2,3])
    add('parenthesized_contextual_literal', 'var x:u32=(((7)));out[(0)]=x;', [7],mutable=True)
    # Comparisons consume runtime f32 reads, including NaNs/infinities. This
    # qualifies only relation instruction semantics, not finite-arithmetic/GPU.
    compare={'eq':('==',lambda x,y:x==y),'ne':('!=',lambda x,y:x!=y),'lt':('<',lambda x,y:x<y),'le':('<=',lambda x,y:x<=y),'gt':('>',lambda x,y:x>y),'ge':('>=',lambda x,y:x>=y)}
    for name,(op,relation) in compare.items():
        vectors=[]
        for left,right in [(0,0x80000000),(0x3f800000,0x40000000),(0x40000000,0x3f800000),(0x7fc00001,0x3f800000),(0x3f800000,0x7fc00001),(0x7f800000,0xff800000)]:
            vectors.append(trial({(0,0):[S],(0,1):[left,right]}, {(0,0):{0:1 if relation(bits_f32(left),bits_f32(right)) else 2}}))
        add('runtime_f32_'+name,f'if a[0]{op}a[1] {{out[0]=1u;}}else{{out[0]=2u;}}',bindings=[storage('out',0),storage('a',1,scalar=2,access=1)],vectors=vectors)
    add('dynamic_infinite_syntactic_break', 'loop {if false {break;}}', unfinished=True,loops=True)
    add('dynamic_infinite_while_true', 'while true {}', unfinished=True,loops=True)
    return cases


def negatives() -> list:
    """Return exact source and required error category from the frozen contract."""
    cases=[]
    def add(name: str,body: str,status: int=4) -> None:
        cases.append((name,program(body,bindings=[storage('out',0)])[0],status))
    for name,body in [
        ('same_scope_duplicate','var x=1u;let x=2u;'),
        ('parameter_duplicate','let gid=1u;'),
        ('self_before_declaration','var x:u32=x;'),
        ('use_before_declaration','out[0]=x;var x=1u;'),
        ('sibling_scope_leak','{let x=1u;}out[0]=x;'),
        ('for_init_scope_leak','for(var i=0u;i<1u;i++){}out[0]=i;'),
        ('for_body_scope_not_update','for(var i=0u;i<1u;i+=body){let body=1u;}'),
        ('immutable_assignment','let x=1u;x=2u;'),
        ('immutable_increment','let x=1u;x++;'),
        ('nonbool_condition','if 1u {out[0]=1u;}'),
        ('bool_numeric','var x=true;out[0]=x+1u;'),
        ('mixed_explicit_types','var x:f32=1u;'),
        ('float_increment','var x:f32=1.0;x++;'),
        ('break_outside','break;'),('continue_outside','continue;'),
        ('continuing_return','loop {continuing {return;}}'),
        ('continuing_nested_return','loop {continuing {if true {return;}break if true;}}'),
        ('continuing_outer_break','loop {continuing {break;}}'),
        ('continuing_outer_continue','loop {continuing {continue;}}'),
        ('break_if_outside','break if true;'),
        ('break_if_not_final','loop {continuing {break if true;out[0]=2u;}}'),
        ('break_if_nested','loop {continuing {{break if true;}}}'),
        ('continue_bypasses_declaration','loop {if gid.x==0u {continue;}let x=3u;continuing {out[0]=x;break if true;}}'),
        ('continue_bypass_dead_condition','loop {if false {continue;}let x=3u;continuing {out[0]=x;break if true;}}'),
        ('continue_bypass_nested_reference','loop {continue;let x=3u;continuing {if true {out[0]=max(x,2u);}break if true;}}'),
        ('loop_empty_behavior','loop {}'),('loop_continue_only','loop {continue;}'),
        ('dead_empty_loop','return;loop {}'),
        ('dead_invalid_type','return;var x:bool=1u;'),
        ('dead_unknown_name','return;out[0]=missing;'),
        ('short_circuit_invalid_rhs','if true || missing {return;}'),
        ('shadowed_max','let max=1u;out[0]=max(1u,2u);'),
        ('shadowed_type','let u32=1u;var x:u32=1u;'),
    ]:add(name,body)
    for name,body in [
        ('inferred_var_abstract','var x=1;'),('inferred_let_abstract','let x=1;'),
        ('abstract_subtraction_subtree','var x:u32=(1-2)+1u;'),
        ('abstract_addition_subtree','var x:u32=(1+2)+1u;'),
        ('abstract_max_subtree','var x:u32=max(1,2)+1u;'),
        ('float_constant_compare','if 1.0<2.0 {return;}'),
        ('float_suffixed_compare','if 1.0f==1.0f {return;}'),
        ('mixed_abstract_float_compare','if 1<2.0 {return;}'),
        ('float_constant_arithmetic','var x:f32=1.0+2.0;'),
        ('abstract_dead_subtree','return;out[0]=(1+2)+1u;'),
        ('dead_float_constant_compare','if false && (1.0==1.0) {return;}'),
        ('unsupported_switch','switch gid.x {}'),('unsupported_local_const','const x=1u;'),
        ('unsupported_division','out[0]=4u/2u;'),('unsupported_unary_minus','out[0]=-1u;'),
    ]:add(name,body,3)
    # Mixed chains and malformed control placement may be diagnosed by parser.
    for name,body in [('mixed_logical_chain','if true && false || true {return;}'),('chained_comparison','if 1u<2u<3u {return;}'),('missing_var_type_initializer','var x;'),('let_missing_initializer','let x:u32;')]:add(name,body,2)
    for name,body in [('for_direct_continuing','for(;;){continuing {break if true;}}'),('while_direct_continuing','while true {continuing {break if true;}}'),('continuing_not_last','loop {continuing {break if true;}out[0]=1u;}'),('duplicate_continuing','loop {continuing {break if true;}continuing {break if true;}}')]:add(name,body,4)
    # Invalid admitted literal magnitude/index are semantic, not silent truncate.
    add('unsuffixed_index_signed_range','out[2147483648]=1u;',3)
    add('unsuffixed_u32_overflow','var x:u32=4294967296;',3)
    # The frozen contract requires UNSUPPORTED only where excluded grammar is
    # recognized. These three unrecognized forms may fail parser admission;
    # neither status permits emission. A concrete out-of-range literal is an
    # invalid value, not a newly unsupported numeric type.
    categories={'unsupported_switch':(2,3),'unsupported_local_const':(2,3),
                'unsupported_unary_minus':(2,3),'unsuffixed_u32_overflow':4}
    cases=[(name,source,categories.get(name,status))
           for name,source,status in cases]
    return cases
