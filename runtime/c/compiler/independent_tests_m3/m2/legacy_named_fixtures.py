"""Retained named-u32-constant fixtures, with unchanged expectations."""
from __future__ import annotations

from legacy_fixtures import source

def cases():
    valid=[];invalid=[]
    for k in [0,1,4294967295]:
        for n in [1,256]:
            for alias in [False,True]:
                const='step_'+str(k);b='items';f='work';gid='pos'
                s=f'const {const}: u32 = {k}u;\n'+source(b,f,gid,n,7,alias,True).replace('+ 7u','+ '+const)
                valid.append((f'constant_{k}_{n}_{alias}',s.encode(),dict(storage=b,entry=f,n=n,k=k)))
    s='/* leading */ const/*a*/ delta /*b*/:u32/*c*/=4294967295u;\n'+source('items','work','pos',256,7,True).replace('+ 7u','+/*nested /* comment */ tail*/delta')
    valid.append(('commented_constant',s.encode(),dict(storage='items',entry='work',n=256,k=4294967295)))
    valid.append(('unused_constant_literal_rhs',('const unused: u32 = 13u;\n'+source(k=7)).encode(),dict(storage='values',entry='increment',n=64,k=7)))
    valid.append(('constant_shadowed_parameter_unused',('const invocation: u32 = 13u;\n'+source(k=7)).encode(),dict(storage='values',entry='increment',n=64,k=7)))
    for const in ['d'*63,'compute','read_write']:
        s=f'const {const}: u32 = 17u;\n'+source().replace('+ 1u','+ '+const)
        valid.append(('constant_name_'+const[:10],s.encode(),dict(storage='values',entry='increment',n=64,k=17)))
    full='const delta: u32 = 7u;\n'+source().replace('+ 1u','+ delta')
    full+=' '*(65536-len(full))
    valid.append(('constant_exact_source_limit',full.encode(),dict(storage='values',entry='increment',n=64,k=7)))
    base='const delta: u32 = 7u;\n'+source().replace('+ 1u','+ delta')
    invalid.extend([
        ('unknown_constant_use',source().replace('+ 1u','+ missing')),
        ('mismatched_constant_use',base.replace('+ delta','+ missing')),
        ('constant_parameter_shadow',base.replace('delta','invocation')),
        ('constant_storage_collision',base.replace('delta','values')),
        ('constant_entry_collision',base.replace('delta','increment')),
        ('constant_required_type_shadow',base.replace('delta','u32')),
        ('constant_overflow',base.replace('= 7u','= 4294967296u')),
        ('constant_negative',base.replace('= 7u','= -1u')),
        ('constant_missing_suffix',base.replace('= 7u','= 7')),
        ('constant_wrong_type',base.replace(': u32',': i32',1)),
        ('constant_inferred_type',base.replace(': u32','',1)),
        ('constant_expression',base.replace('= 7u','= 1u + 6u')),
        ('constant_alias_initializer','const delta: u32 = other;\n'+source().replace('+ 1u','+ delta')),
        ('second_constant','const other: u32 = 2u;\n'+base),
        ('reserved_constant_name',base.replace('delta','asm')),
        ('invalid_constant_name',base.replace('delta','__delta')),
        ('oversized_constant_name',base.replace('delta','d'*64)),
        ('constant_after_storage',base.replace('const delta: u32 = 7u;\n','').replace('@compute','const delta: u32 = 7u;\n@compute')),
        ('constant_after_function',base.replace('const delta: u32 = 7u;\n','')+'const delta: u32 = 7u;'),
        ('constant_in_function',source().replace('+ 1u','+ delta').replace('{\n','{\nconst delta: u32 = 7u;\n')),
        ('constant_used_workgroup',base.replace('workgroup_size(64)','workgroup_size(delta)')),
        ('constant_used_index',base.replace('[invocation.x]','[delta]')),
    ])
    return valid,[(n,s.encode()) for n,s in invalid]
