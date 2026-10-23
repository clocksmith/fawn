#!/usr/bin/env python3
"""Enumerated evaluator repairs after v3 observation; originals stay sealed."""
from __future__ import annotations
import argparse,copy,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'source'))
from run_m10 import cases,evaluate
from fixtures_m10 import inventory
from model_m10 import expected_writes
from abi_m10 import Compiler
from seal_m10 import verify_seal

TOPLEVEL_PARSE={'f16','workgroup','private','alias','override'}
def corrected_cases():
    original=cases();result=[];changes=[];partial=None
    for case in original:
        c=copy.copy(case)
        if c['name'] in TOPLEVEL_PARSE:
            assert c['status']==3;c['status']=2
            changes.append(dict(name=c['name'],kind='expected_status',old=3,new=2,reason='Fresh accepted M9 dee5f42 and v3 both return historical PARSE_ERROR for this exact excluded top-level declaration. No other category is permitted.'))
        if c['name'].startswith('runtime_vector_load_3_'):
            assert len(c['buffers'][(0,1)])==44
            if c['name']=='runtime_vector_load_3_2':
                partial=copy.copy(c);partial['name']+='_partial44';partial['writes']={(0,0):expected_writes([0,0,0])};partial['tags']=[*c['tags'],'postdisclosure_partial_stride_probe']
            c['buffers']=dict(c['buffers']);c['buffers'][(0,1)]+=bytes([0xD7])*4
            changes.append(dict(name=c['name'],kind='input_binding_extent',old=44,new=48,reason='Three complete vec3<u32> array elements require3*16 bytes;44 bytes expose only floor(44/16)=2 elements. Offset/stride and expected values unchanged.'))
        result.append(c)
    assert partial is not None;result.append(partial)
    return result,changes

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--library',required=True,type=Path);p.add_argument('--validator',required=True);p.add_argument('--receipt',required=True,type=Path);a=p.parse_args()
    verify_seal()
    if a.receipt.exists():p.error('fresh absent receipt required')
    fixtures,changes=corrected_cases();compiler=Compiler(a.library.resolve());rows=[]
    for case in fixtures:
        try:row=evaluate(compiler,case,a.validator);row['outcome']='passed'
        except Exception as error:row=dict(name=case['name'],outcome='failed',reason=repr(error))
        rows.append(row)
    failed=[r['name'] for r in rows if r['outcome']=='failed']
    result=dict(format='m10-postdisclosure-enumerated-adapter-v1',original_sealed_manifest='fac2e3ce9f8869257d21d6c31b4ad79758c18c03426115e85013f03ba96aacb5',original_failures_preserved=True,candidate_knowledge='v3 original575-case run and fresh accepted M9 status baseline',adapter_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),library_sha256=hashlib.sha256(a.library.read_bytes()).hexdigest(),repairs=changes,added_cases=['runtime_vector_load_3_2_partial44'],expected_inventory_sha256=hashlib.sha256(json.dumps(inventory(fixtures),sort_keys=True).encode()).hexdigest(),total=len(rows),passed=len(rows)-len(failed),failed=failed,cases=rows,physical_device='unrun',matrix_annotation_expectation='Unchanged UNSUPPORTED3 in separately sealed native suite; v3 regression remains a blocker.')
    a.receipt.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n');raise SystemExit(bool(failed))
