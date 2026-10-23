"""Additive post-black-box scope/decimal probes; never weaken frozen expectations."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import random
from evaluate import Evaluator
from m2_fixtures import program, A, B, O, S
from m4_fixtures import expected_vector
from rational_oracle import decimal_bits


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for key in ['library','validator','out']:p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);e=Evaluator(a.library,a.out/'modules',a.validator)
    initial={A:[],B:[],O:[S,S]}
    positives=[('own_u32_annotation','let u32: u32 = 7u; out[0u] = u32;',1,{O:{0:7}}),
        ('own_f32_annotation','let f32: f32 = 0.1; out[0u] = f32;',2,{O:{0:decimal_bits('0.1')}}),
        ('builtin_own_initializer','let max = max(2u, 3u); out[0u] = max;',1,{O:{0:3}}),
        ('scope_name_reintroduced','if (gid.x == 0u) { let value = 1u; out[0u] = value; } let value = 2u; out[1u] = value;',1,{O:{0:1,1:2}}),
        ('type_scope_restored','if (gid.x == 0u) { let u32 = 1u; out[0u] = u32; } let value: u32 = 2u; out[1u] = value;',1,{O:{0:1,1:2}}),
        ('max_scope_restored','if (gid.x == 0u) { let max = 1u; out[0u] = max; } out[1u] = max(2u, 3u);',1,{O:{0:1,1:3}})]
    for name,body,scalar,changes in positives:
        source,ex=program(body,scalar=scalar,alias=False);ex['vectors']=[expected_vector(0,initial,changes)];e.valid(name,source,ex)
    negatives=[('parameter_max','out[0u] = max(1u, 2u);',dict(parameter='max')),
        ('entry_max','out[0u] = max(1u, 2u);',dict(entry='max')),
        ('constant_max','out[0u] = max(1u, 2u);',dict(constant='const max: u32 = 3u;\n')),
        ('alias_max','let max = gid.x; out[0u] = max(1u, 2u);',{}),
        ('alias_type','let u32 = gid.x; let value: u32 = 1u; out[0u] = value;',{}),
        ('parameter_type','let value: u32 = 1u; out[0u] = value;',dict(parameter='u32')),
        ('nested_type','let u32 = 1u; if (gid.x == 0u) { let value: u32 = 2u; }',{}),
        ('dead_shadow_call','return; let max = 1u; out[0u] = max(1u, 2u);',{})]
    for name,body,kw in negatives:e.invalid(name,program(body,alias=False,**kw)[0])
    rng=random.Random(0xd0e449);decimals=set()
    for after in range(1,9):
        for _ in range(40):
            number=rng.randrange(10**8,10**9);digits=str(number);decimals.add(digits[:-after]+'.'+digits[-after:])
    for i,text in enumerate(sorted(decimals)):
        source,ex=program('out[0u] = '+text+';',scalar=2);ex['vectors']=[expected_vector(0,initial,{O:{0:decimal_bits(text)}})]
        e.valid('decimal_sweep_'+str(i),source,ex)
    (a.out/'results.json').write_text(json.dumps(dict(checks=len(e.records),passed=sum(r['passed'] for r in e.records),cpu_trials=e.cpu_trials,decimal_count=len(decimals),records=e.records),indent=2)+'\n')
    print(len(e.records),'checks',sum(r['passed'] for r in e.records),'passed',e.cpu_trials,'instruction trials');return int(not all(r['passed'] for r in e.records))
if __name__=='__main__':raise SystemExit(main())
