#!/usr/bin/env python3
"""Explicit opt-in parser transition and target instruction-width boundaries.

Large type widths generate source tokens only. No aggregate values are flattened
or recursively packed by this evaluator. These checks are not ordinary-suite
profile promises and need their recorded explicit caller capacities.
"""
from __future__ import annotations
import argparse,hashlib,json
from pathlib import Path
from abi_m10 import Compiler,profile
from seal_m10 import verify_seal

def parser_cases():
    expression='1u'
    for _ in range(12):expression='array<u32,'+expression+'>(1u)[0]'
    source='@compute @workgroup_size(1) fn main(){var a:array<u32,'+expression+'>;}'
    result=[]
    for name,expr,typ,pool in [('global_expression_counter',2,64,17),('global_type_counter',64,2,19),('both_counters_admitted',64,64,0)]:
        caps=profile();caps.expression_depth=expr;caps.type_syntax_depth=typ
        result.append(dict(name=name,source=source,caps=caps,status=1 if pool else 0,pool=pool))
    return result

def width_case(count):
    source='struct Wide{'+''.join(f'f{i}:u32,' for i in range(count))+'}@compute @workgroup_size(1) fn main(){}'
    caps=profile();caps.source_bytes=len(source);caps.tokens=5*count+128;caps.members=count+4
    caps.types=64;caps.resources=8;caps.traversal_items=2*count+128;caps.target_declarations=count+512;caps.words=8*count+2048
    return dict(name=f'OpTypeStruct_members_{count}',source=source,caps=caps,status=0 if count<=65533 else 3,pool=0)

def run(library,include_width=False):
    cases=parser_cases()+([width_case(65533),width_case(65534)] if include_width else [])
    rows=[]
    for c in cases:
        compiler=Compiler(library,c['caps']);out=[]
        for flags in (5,6,7):
            status,r,_=compiler.call(c['source'],flags)
            assert status==c['status'],(c['name'],status,bytes(r.diagnostic.message))
            if c['pool']:assert r.exhausted_pool==c['pool'] and r.minimum_pool_capacity>0 and not r.required_is_exact
            if status==0:assert r.required_is_exact==1
            if status==3:assert not r.exhausted_pool and not r.required_is_exact
            out.append(dict(flags=flags,status=status,pool=r.exhausted_pool,minimum=r.minimum_pool_capacity,required=r.required.fields()))
        rows.append(dict(name=c['name'],source_sha256=hashlib.sha256(c['source'].encode()).hexdigest(),source_bytes=len(c['source']),capacities=c['caps'].fields(),results=out))
    return dict(format='m10-boundaries-v1',cases=rows,target_width='run' if include_width else 'unrun',target_uint32_ID_exhaustion='not dynamically feasible; actual append/width/ID source audit required',derived_type_syntax_pool='TS=floor(tokens/2), not independently configurable; token/syntax accounting source audit required')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--target-width',action='store_true');p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
    if a.receipt.exists():p.error('fresh absent receipt required')
    verify_seal()
    try:report=run(a.library,a.target_width)
    except Exception as error:
        a.receipt.write_text(json.dumps(dict(outcome='failed',reason=repr(error)),indent=2)+'\n');raise
    a.receipt.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
