"""Exact M10 admission lifts discovered by unchanged historical replay.
Max builtin/nominal collision remains negative, never a lift.
"""
import copy,sys,struct
import historical_bridge_v2 as bridge
semantic,retained,corpus=bridge.install()
import retained_m9,retained_m8 as old
from fixtures_m5 import storage,packed
from fixtures_m6 import trial
LIFTS={'m4':{'vector_local'},'m5':{'struct_constructor','align_member','size_member'},'m6':{'vector_let'},'m7':set()}
def lifted(family,name,source):
 e=copy.deepcopy(old.metadata_from_frozen_source(family,name,source))
 if family=='m5':
  if name=='align_member':
   e['structs'][0].update(size_bytes=16,alignment_bytes=16);e['structs'][0]['members'][0]['alignment_bytes']=16
   e['bindings'][0].update(minimum_binding_size_bytes=16,type_alignment_bytes=16)
  if name=='size_member':
   e['structs'][0]['size_bytes']=24;e['structs'][0]['members'][0]['size_bytes']=16
   e['structs'][0]['members'][1]['offset_bytes']=16;e['structs'][0]['members'][2]['offset_bytes']=20
   e['bindings'][0]['minimum_binding_size_bytes']=24
 vectors=[]
 for x in (0,1,0xffffffff):
  for length in (0,1,4):
   initial={(b['group'],b['binding']):[0xa55a36c9]*length for b in e['bindings'] if b['kind']==1};changes={};uniforms={}
   if family=='m5':
    raw=bytearray(e['structs'][0]['size_bytes'])
    for m,v in zip(e['structs'][0]['members'],(7,0x40000000,11)):struct.pack_into('<I',raw,m['offset_bytes'],v)
    uniforms[(0,0)]=bytes(raw)
    if name!='struct_constructor':changes={(0,1):{0:7}}
   if family=='m4':changes={(0,2):{0:1}}
   vectors.append(trial(initial,changes,inputs={28:(x,2,3)},uniforms=uniforms,synthetic=True))
 e['vectors']=vectors;return e
original_configure=retained_m9.configure
original_lift=retained_m9.lifted
def configure():
 original_configure()
 for family,names in LIFTS.items():old.NEW_LIFTS[family]|=names
 old.lift_expected=lambda f,n,s:lifted(f,n,s) if n in LIFTS[f] else original_lift(f,n,s)
retained_m9.configure=configure
original_cases=semantic.cases
def vector_lift_expected(name):
 bindings=[storage('out',0)]
 if name=='excluded_numeric_feature_3':bindings.append(storage('src',1,access=1))
 return dict(entry='main',n=[1,1,1],bindings=bindings,structs=[],builtins=[],vectors=[trial({(b['group'],b['binding']):[0xa55a36c9]*length for b in bindings},{},inputs={},synthetic=True) for length in (0,1,4)])
def cases(repo,stage):
 good,bad=original_cases(repo,stage);stay=[]
 for n,s,st in bad:
  if n in {'helper_vector_formal','helper_vector_result'}:good.append((n,s,vector_lift_expected(n)))
  else:stay.append((n,s,st))
 return good,stay
semantic.cases=cases
original_m8=retained_m9.m8_cases
def m8_cases(repo):
 good,bad=original_m8(repo);stay=[]
 for n,s,st in bad:
  if n=='excluded_numeric_feature_3':good.append((n,s,vector_lift_expected(n)))
  else:stay.append((n,s,st))
 return good,stay
retained_m9.m8_cases=m8_cases
if __name__=='__main__':
 stage=sys.argv.pop(1)
 raise SystemExit(semantic.main() if stage=='m9' else retained_m9.main())
