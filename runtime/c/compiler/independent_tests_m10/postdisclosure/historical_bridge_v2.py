"""Postdisclosure representation bridge: frozen historical expectations, M10 target interpreter.
No target rewriting, source interpretation, reflected tags, or production layout code.
"""
from pathlib import Path
import struct,sys
ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'source'))
from target_m10 import Module as Target,Invalid,Unsupported,Unfinished,literal
from spirv_oracle_base import StructuralError,ExecutionError
from compute_oracle_m9 import StepLimit,BindingAdmissionError,Module as ScalarModule
class Module(Target):
 def __init__(self,words):
  super().__init__(words);self.instructions=self.ins;self.last_steps=0;self.last_opcodes={};self.last_calls=[];self.uniform_minimums={};self.scalar_oracle=None
 def interface(self,e):
  if not any(b.get('kind',1)==2 for b in e['bindings']):
   import retained_m8
   self.scalar_oracle=ScalarModule(list(self.words));return self.scalar_oracle.interface(retained_m8.normalized(e))
  n=e['n'] if isinstance(e['n'],list) else [e['n'],1,1]
  assert self.words[1]==0x10300 and tuple(n)==self.local
  entries=[a for op,a,_ in self.ins if op==15];assert len(entries)==1
  a=entries[0];assert literal(a[2:])==e['entry']
  assert [a for op,a,_ in self.ins if op==14]==[(0,1)]
  assert any(op==17 and a==(1,) for op,a,_ in self.ins)
  expected={(b['group'],b['binding']):b for b in e['bindings']}
  assert len(self.resources)==len(set(self.resources.values())) and set(self.resources.values())==set(expected)
  for rid,key in self.resources.items():
   b=expected[key];ptr,sc,_=self.variables[rid];assert self.types[ptr][0]==32 and self.types[ptr][1][0]==sc
   root=self.types[ptr][1][1];op,members=self.types[root];assert op==30 and 2 in self.decor.get(root,{})
   if b.get('kind',1)==1:
    assert sc==12 and len(members)==1 and self.member[(root,0)][35]==(0,)
    arr=members[0];assert self.types[arr][0]==29 and self.decor[arr][6]==(4,)
    assert self.types[self.types[arr][1][0]]==((21,(32,0)) if b['scalar']==1 else (22,(32,)))
    if b['access']==1:assert 24 in self.decor.get(rid,{}) or 24 in self.member.get((root,0),{})
   else:
    assert sc==2
    # Both legal emitted representations checked explicitly, without mutation.
    if len(members)==1 and self.types[members[0]][0]==30:
     assert self.member[(root,0)][35]==(0,);root=members[0];members=self.types[root][1]
    spec=e['structs'][b['struct_index']];assert len(members)==len(spec['members'])
    for i,(typ,m) in enumerate(zip(members,spec['members'])):
     assert self.types[typ]==((21,(32,0)) if m['scalar']==1 else (22,(32,)))
     assert self.member[(root,i)][35]==(m['offset_bytes'],)
    self.uniform_minimums[key]=b['minimum_binding_size_bytes']
  assert set(self.inputs.values())==set(e.get('builtins',[28]))
  for rid,b in self.inputs.items():
   ptr,sc,_=self.variables[rid];assert sc==1;typ=self.types[ptr][1][1]
   if b==29:assert self.types[typ]==(21,(32,0))
   else:
    op,parts=self.types[typ];assert op==23 and parts[1]==3 and self.types[parts[0]]==(21,(32,0))
  return dict(word_count=len(self.words),instruction_count=len(self.ins),opcodes=sorted({op for op,a,p in self.ins}),oracle='sealed M10 target interpreter; postdisclosure structural/byte adapter')
 def execute(self,buffers,builtins,read_only,uniforms=None,step_limit=1000000):
  if self.scalar_oracle is not None:
   result=self.scalar_oracle.execute(buffers,builtins,read_only,uniforms,step_limit)
   self.last_steps=self.scalar_oracle.last_steps;self.last_opcodes=self.scalar_oracle.last_opcodes;self.last_calls=self.scalar_oracle.last_calls;return result
  uniforms={} if uniforms is None else uniforms
  for key,minimum in self.uniform_minimums.items():
   if key not in uniforms or len(uniforms[key])<minimum:raise BindingAdmissionError('short uniform')
  raw={k:bytearray(struct.pack('<'+'I'*len(v),*v)) for k,v in buffers.items()};raw.update({k:bytearray(v) for k,v in uniforms.items()})
  try:r=super().execute(raw,builtins,read_only,step_limit)
  except Unfinished as ex:raise StepLimit(str(ex)) from ex
  except (Invalid,Unsupported) as ex:raise ExecutionError(str(ex)) from ex
  self.last_steps=r['steps'];self.last_opcodes=r['opcounts'];self.last_calls=[(a,b,(),d) for a,b,d in r['calls']]
  for k,v in buffers.items():v[:]=struct.unpack('<'+'I'*(len(raw[k])//4),raw[k])
  assert all(bytes(raw[k])==v for k,v in uniforms.items())
  return [('uniform_read',k,off) if k in uniforms else (op,k,off//4) for op,k,off,size in r['accesses']]
class LegacyAdapter(Module):
 def execute(self,buffers,x,read_only,y=37,z=91,uniforms=None):return super().execute(buffers,{28:(x,y,z)},read_only,uniforms)
def install():
 import semantic_m9,retained_m9,retained_m9_corrected,retained_m8,retained_m7,corpus_m8,corpus_m9
 semantic_m9.Module=Module;retained_m9.Module=Module;retained_m9.LegacyAdapter=LegacyAdapter
 retained_m8.Module=Module;retained_m8.LegacyAdapter=LegacyAdapter;retained_m7.ComputeModule=Module
 corpus_m9.Module=Module;corpus_m9.LegacyAdapter=LegacyAdapter
 return semantic_m9,retained_m9_corrected,corpus_m9
if __name__=='__main__':
 stage=sys.argv.pop(1);semantic,retained,corpus=install()
 raise SystemExit({'m9':semantic.main,'historical':retained.original.main,'m8':retained.original.main,'corpus':corpus.main}[stage]())
