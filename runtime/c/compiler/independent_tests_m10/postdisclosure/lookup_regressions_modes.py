#!/usr/bin/env python3
"""Postdisclosure precise scope-order probes. No frozen test is changed."""
import argparse,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'source'))
from abi_m10 import Compiler
p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args()
CASES=[('nominal_max_wrong_arity','struct max{x:u32}@compute @workgroup_size(1) fn main(){let x=max(1u,2u);}',4),('nominal_max_constructor','struct max{x:u32}@compute @workgroup_size(1) fn main(){let x:max=max(1u);}',0),('nominal_array_constructor','struct array{x:u32}@compute @workgroup_size(1) fn main(){let x:array=array(1u);}',0),('nominal_vec2_constructor','struct vec2{x:u32}@compute @workgroup_size(1) fn main(){let x:vec2=vec2(1u);}',0),('nominal_bitcast_constructor','struct bitcast{x:u32}@compute @workgroup_size(1) fn main(){let x:bitcast=bitcast(1u);}',0)]
rows=[];c=Compiler(Path(a.library))
for name,source,status in CASES:
 modes=[]
 for flags in (1,2,3,5,6,7):
  actual,r,_=c.call(source,flags,dict(words=65536 if flags&1 else 0,types=64 if flags&2 else 0,members=64 if flags&2 else 0,resources=8 if flags&2 else 0,name_bytes=1024 if flags&2 else 0) if not flags&4 else None);modes.append(dict(flags=flags,status=actual,offset=r.diagnostic.offset,diagnostic=bytes(r.diagnostic.message).decode(),passed=actual==status))
 rows.append(dict(name=name,source=source,source_sha256=hashlib.sha256(source.encode()).hexdigest(),expected_status=status,modes=modes))
assert not a.receipt.exists();a.receipt.write_text(json.dumps(dict(provenance='Postdisclosure from unchanged M5 max_type_shadow_builtin failure; expected lookup derived from frozen M10 §2 module-before-predeclared rule.',library_sha256=hashlib.sha256(Path(a.library).read_bytes()).hexdigest(),cases=rows),indent=2)+'\n')
print(json.dumps(rows,indent=2))
