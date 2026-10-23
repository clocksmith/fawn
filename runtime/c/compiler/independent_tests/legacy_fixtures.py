"""Retained baseline fixture definitions, copied without altering expectations."""
from __future__ import annotations

import random
import re

def source(storage='values',entry='increment',ident='invocation',n=64,k=1,alias=False,reverse=False):
    resource='@binding(0) @group(0)' if reverse else '@group(0) @binding(0)'
    compute=f'@workgroup_size({n}) @compute' if reverse else f'@compute @workgroup_size({n})'
    vector='vec3u' if alias else 'vec3<u32>'
    return f'{resource} var<storage, read_write> {storage}: array<u32>;\n{compute} fn {entry}(@builtin(global_invocation_id) {ident}: {vector}) {{\n  {storage}[{ident}.x] = {storage}[{ident}.x] + {k}u;\n}}\n'
SEED=source()

def admitted():
    cases=[]
    def add(name,s,storage='values',entry='increment',n=64,k=1): cases.append((name,s.encode(),dict(storage=storage,entry=entry,n=n,k=k)))
    add('seed',SEED)
    for i,(n,k) in enumerate([(1,0),(1,1),(2,4294967295),(32,2147483648),(64,4294967294),(255,17),(256,4294967295)]):
        b=f'buffer_{i}';f=f'entry_{i}';gid=f'_gid{i}'
        add(f'variant_{i}',source(b,f,gid,n,k,i%2==0,i%3==0),b,f,n,k)
    for name in ['x','Z9','_x','compute','binding','read_write','global_invocation_id','a'*63]:
        add('storage_identifier_'+name[:10],source(storage=name),storage=name)
    for name in ['x','_x','compute','binding','global_invocation_id','e'*63]:
        add('entry_identifier_'+name[:10],source(entry=name),entry=name)
    add('parameter_shadows_entry',source(ident='increment'))
    for name in ['u32','array','vec3','vec3u']:
        add('parameter_type_spelling_'+name,source(ident=name))
    add('unused_type_vec3',source(storage='vec3',alias=True),storage='vec3')
    add('unused_type_vec3u',source(entry='vec3u',alias=False),entry='vec3u')
    add('max_identifier_triplet',source('b'*63,'e'*63,'i'*63),storage='b'*63,entry='e'*63)
    add('compact',re.sub(r'\s*([@()<>,:;{}\[\].=+])\s*',r'\1',SEED))
    toks=re.findall(r'[A-Za-z_][A-Za-z_0-9]*|[0-9]+u?|[^\s]',SEED)
    add('comment_every_token','/* leading */'+ '/* x /* y */ z */'.join(toks)+'// trailing')
    add('line_comment_every_token','// leading\n'+'// token\n'.join(toks)+'// trailing')
    add('tabs_newlines','\t\n\n'+SEED.replace(' ','\t').replace('\n','\n\n'))
    add('no_final_newline',SEED.rstrip())
    add('line_comment_no_final_newline',SEED+'// last line')
    for depth in [1,8,1024,16000]: add(f'nested_comment_depth_{depth}','/*'*depth+' nested '+'*/'*depth+SEED)
    add('exact_source_limit',SEED+' '*(65536-len(SEED)))
    rng=random.Random(0xD0E2026)
    for i in range(12):
        n=rng.randint(1,256);k=rng.randint(0,2**32-1);b='buf'+str(i);f='run'+str(i);gid='gid'+str(i)
        s=source(b,f,gid,n,k,bool(i%2),bool(i%3));s='/* randomized lexical variation */\n'+s.replace(' ',rng.choice(['\t','\n','/* gap */']))
        add(f'randomized_{i}',s,b,f,n,k)
    return cases

def rejected():
    cases=[]
    def add(name,s,offset=None,status=None):
        cases.append((name,s.encode() if isinstance(s,str) else s,offset,status))
    def mutate(name,a,b,count=-1): add(name,SEED.replace(a,b,count))
    add('empty',b'');add('whitespace_only',' \t\n');add('comment_only','/* a /* b */ c */')
    for n in [0,257,4294967296,999999999999999999999999]: mutate('workgroup_'+str(n),'workgroup_size(64)',f'workgroup_size({n})')
    for k in ['01u','00u','4294967296u','18446744073709551616u','999999999999999999999999999999999999u','1','1i','1f','-1u','+1u','1.0u','0x1u','1_0u','1uu','1ufoo']:
        mutate('literal_'+k,'+ 1u','+ '+k)
    mutate('unsupported_stage','@compute','@fragment');mutate('unsupported_address_space','var<storage,','var<uniform,');mutate('unsupported_access','read_write','read');mutate('unsupported_type','array<u32>','array<i32>');mutate('unsupported_fixed_array','array<u32>','array<u32, 4>');mutate('unsupported_parameter_type','vec3<u32>','vec4<u32>');mutate('unsupported_parameter_scalar','vec3<u32>','vec3<i32>');mutate('unsupported_builtin','global_invocation_id','local_invocation_id')
    mutate('leading_zero_workgroup','workgroup_size(64)','workgroup_size(064)');mutate('leading_zero_binding','@binding(0)','@binding(00)');mutate('group_nonzero','@group(0)','@group(1)');mutate('binding_nonzero','@binding(0)','@binding(1)');mutate('binding_negative','@binding(0)','@binding(-1)');mutate('binding_overflow','@binding(0)','@binding(4294967296)')
    for name,a,b in [('group_duplicate','@group(0)','@group(0) @group(0)'),('binding_duplicate','@binding(0)','@binding(0) @binding(0)'),('compute_duplicate','@compute','@compute @compute'),('workgroup_duplicate','@compute','@workgroup_size(32) @compute'),('unknown_attribute','@compute','@unknown @compute'),('group_missing','@group(0)',''),('binding_missing','@binding(0)',''),('compute_missing','@compute',''),('workgroup_missing','@workgroup_size(64)',''),('workgroup_extra_dimension','workgroup_size(64)','workgroup_size(64,1)'),('builtin_duplicate','@builtin(global_invocation_id)','@builtin(global_invocation_id) @builtin(global_invocation_id)')]:mutate(name,a,b)
    mutate('lhs_unknown','  values[','  other[',1);mutate('rhs_unknown','= values[','= other[',1);mutate('index_unknown','values[invocation.x]','values[missing.x]',1);mutate('index_mismatch','= values[invocation.x]','= values[missing.x]',1)
    mutate('wrong_component','.x','.y');mutate('wrong_operator','+ 1u','- 1u');mutate('extra_expression','+ 1u','+ 1u + 1u');mutate('compound_assignment','= values[invocation.x] + 1u','+= 1u');mutate('missing_semicolon','1u;','1u');mutate('missing_close_brace','}\n','');mutate('extra_statement','1u;','1u; values[invocation.x] = values[invocation.x] + 1u;');mutate('empty_body','  values[invocation.x] = values[invocation.x] + 1u;','');mutate('return_statement','  values[invocation.x] = values[invocation.x] + 1u;','return;');mutate('parameter_extra','invocation: vec3<u32>','invocation: vec3<u32>, extra: u32')
    add('trailing_identifier',SEED+'junk');add('trailing_function',SEED+'fn extra() {}');add('trailing_statement',SEED+';');add('reversed_declarations',SEED[SEED.index('@compute'):]+SEED[:SEED.index('@compute')]);add('second_storage',SEED[:SEED.index('@compute')]+SEED)
    for name in ['_', '__values', 'if', 'var', 'fn', 'return', 'true', 'asm', 'NULL', 'namespace', 'class', 'a'*64,'9values','v$alues','évalues']:
        add('invalid_storage_name_'+name[:15],source(storage=name))
    for name in ['_','__entry','if','asm','e'*64]: add('invalid_entry_name_'+name[:10],source(entry=name))
    for name in ['_','__id','if','asm','i'*64]: add('invalid_param_name_'+name[:10],source(ident=name))
    for name in ['u32','array','vec3']:
        add('storage_shadows_used_type_'+name,source(storage=name))
        add('entry_shadows_used_type_'+name,source(entry=name))
    add('storage_shadows_alias',source(storage='vec3u',alias=True))
    add('entry_shadows_alias',source(entry='vec3u',alias=True))
    add('module_name_collision',source(entry='values'));add('parameter_shadows_storage',source(ident='values'))
    add('unterminated_comment',SEED+'/* unclosed');add('unterminated_nested_comment','/* outer /* inner */'+SEED);add('stray_comment_close',SEED+'*/');add('quoted_source','"'+SEED+'"');add('ascii_control',SEED+'\x01');add('nonascii_whitespace',SEED+'\u00a0');add('over_source_limit',SEED+' '*(65537-len(SEED)),status=1);add('excess_tokens',SEED+' ;'*513)
    for where in [0,2,len(SEED)//2,len(SEED)]:
        raw=SEED.encode();add('embedded_nul_'+str(where),raw[:where]+b'\0'+raw[where:],offset=where)
    raw=b'// comment\0 still comment\n'+SEED.encode();add('nul_in_line_comment',raw,offset=10)
    raw=b'/* comment\0 still comment */\n'+SEED.encode();add('nul_in_block_comment',raw,offset=10)
    return cases
