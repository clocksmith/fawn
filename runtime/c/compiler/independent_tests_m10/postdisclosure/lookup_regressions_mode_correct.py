#!/usr/bin/env python3
"""Postdisclosure precise scope-order probes. No frozen test is changed."""
import argparse,hashlib,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'source'))
from abi_m10 import Compiler,Outputs,Result,Type,Member,Resource,U32
import ctypes as C
def call_mode(c,source,flags):
 raw=source.encode();src=C.create_string_buffer(raw);r=Result();o=Outputs();keep=[]
 if flags&4:return c.call(source,flags)
 if flags&1:
  words=(U32*65536)();keep.append(words);o.words=words;o.word_capacity=65536
 if flags&2:
  for typ,n,ptr,cap,size in [(Type,64,"types","type_capacity","type_record_size"),(Member,64,"members","member_capacity","member_record_size"),(Resource,8,"resources","resource_capacity","resource_record_size")]:
   arr=(typ*n)();keep.append(arr);setattr(o,ptr,arr);setattr(o,cap,n);setattr(o,size,C.sizeof(typ))
  names=(C.c_char*1024)();keep.append(names);o.names=names;o.name_capacity=1024
 status=c.compile(4,C.byref(c.caps),C.sizeof(c.caps),c.address,c.size,src,len(raw),flags,C.byref(o),C.sizeof(o),C.byref(r),C.sizeof(r))
 return status,r,keep
p=argparse.ArgumentParser();p.add_argument('--library',required=True);p.add_argument('--receipt',type=Path,required=True);a=p.parse_args()
CASES=[('nominal_max_wrong_arity','struct max{x:u32}@compute @workgroup_size(1) fn main(){let x=max(1u,2u);}',4),('nominal_max_constructor','struct max{x:u32}@compute @workgroup_size(1) fn main(){let x:max=max(1u);}',0),('nominal_array_constructor','struct array{x:u32}@compute @workgroup_size(1) fn main(){let x:array=array(1u);}',0),('nominal_vec2_constructor','struct vec2{x:u32}@compute @workgroup_size(1) fn main(){let x:vec2=vec2(1u);}',0),('nominal_bitcast_constructor','struct bitcast{x:u32}@compute @workgroup_size(1) fn main(){let x:bitcast=bitcast(1u);}',0)]
rows=[];c=Compiler(Path(a.library))
for name,source,status in CASES:
 modes=[]
 for flags in (1,2,3,5,6,7):
  actual,r,_=call_mode(c,source,flags);modes.append(dict(flags=flags,status=actual,offset=r.diagnostic.offset,diagnostic=bytes(r.diagnostic.message).decode(),passed=actual==status))
 rows.append(dict(name=name,source=source,source_sha256=hashlib.sha256(source.encode()).hexdigest(),expected_status=status,modes=modes))
assert not a.receipt.exists();a.receipt.write_text(json.dumps(dict(provenance='Postdisclosure from unchanged M5 max_type_shadow_builtin failure; expected lookup derived from frozen M10 §2 module-before-predeclared rule.',library_sha256=hashlib.sha256(Path(a.library).read_bytes()).hexdigest(),cases=rows),indent=2)+'\n')
print(json.dumps(rows,indent=2))
