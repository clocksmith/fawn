"""Pinned full98 real consumer, exact four admissions and source-bound phase names."""
import sys,struct,hashlib
from pathlib import Path
import historical_bridge_v2 as bridge
semantic,retained,corpus=bridge.install()
import corpus_m7,corpus_m8
from extra_m10 import witnesses
from run_m10 import execute as aggregate_execute
from target_m10 import Module as Target
from exact_numeric_m8 import fp_operation,round_bits
NEW={'bench/kernels/stable_fluids_'+k+'.wgsl':k for k in ('pressure','divergence','project')}
STRESS='bench/kernels/compilation-corpus/stress_many_bindings.wgsl'
original_inventory=corpus.inventory;original_cases=corpus.instruction_cases;original_output=corpus_m8.execute_output;original_phase=corpus_m7.diagnostic_phase

def inventory(repo):
 rows,classes=original_inventory(repo)
 for n in [*NEW,STRESS]:classes[n]=dict(reason='Exact M10 aggregate/function family witness' if n in NEW else 'Exact ABI4 resource-count admission',capability='M10')
 return rows,classes

def instruction_cases(repo):
 cases=original_cases(repo)
 for n,k in NEW.items():
  selected=[c for c in witnesses() if c['name'].startswith('real_'+k+'_')]
  raw=(repo/n).read_bytes();assert len(selected)==6 and all(c['source'].encode()==raw for c in selected)
  cases[hashlib.sha256(raw).hexdigest()]=('m10',n,selected)
 raw=(repo/STRESS).read_bytes() # Exact source hash already checked against frozen98 ledger.
 cases[hashlib.sha256(raw).hexdigest()]=('m10-stress',STRESS,None)
 return cases

def stress(words):
 cases=[]
 keys=[(0,1),(0,2),(0,3),(0,4),(1,0),(1,1),(1,2)]
 for extent in (1,3,5):
  values={k:[round_bits((j+1)*(i+1)) for j in range(max(1,extent-(i%2)))] for i,k in enumerate(keys)}
  buffers={k:bytearray(struct.pack('<'+'I'*len(v),*v)) for k,v in values.items()}
  buffers[(0,0)]=bytearray(struct.pack('<4I',5,round_bits(2),round_bits(3),round_bits(4)))
  for k in ((0,5),(0,6),(1,3)):buffers[k]=bytearray(struct.pack('<'+'I'*extent,*([0xa55a36c9]*extent)))
  wanted={k:bytearray(v) for k,v in buffers.items()}
  def at(k,i):return values[k][i] if i<len(values[k]) else 0
  for i in range(extent):
   a=fp_operation('*',at((0,1),i),at((1,0),i));b=fp_operation('*',at((0,2),i),at((1,1),i));c=fp_operation('*',at((0,3),i),at((1,2),i))
   z=fp_operation('+',fp_operation('+',fp_operation('+',fp_operation('*',a,round_bits(2)),fp_operation('*',b,round_bits(3))),fp_operation('*',c,round_bits(4))),at((0,4),i))
   for k,v in [((0,5),z),((0,6),fp_operation('*',z,z)),((1,3),fp_operation('+',fp_operation('+',a,b),c))]:struct.pack_into('<I',wanted[k],4*i,v)
  m=Target(words);steps=0
  for x in range(8):steps+=m.execute(buffers,{28:(x,0,0)},set(keys)|{(0,0)})['steps']
  assert buffers==wanted;cases.append(dict(extent=extent,invocations=8,steps=steps))
 return dict(fixture=STRESS,cpu_invocations=24,scenarios=cases,oracle='postdisclosure independent exact-f32 byte effects; no reflection tags')

def execute_output(raw,case):
 kind,name,e=case
 if kind not in {'m10','m10-stress'}:return original_output(raw,case)
 words=list(struct.unpack('<'+'I'*(len(raw)//4),raw))
 if kind=='m10-stress':return stress(words)
 proofs=[dict(name=c['name'],**aggregate_execute(c,words)) for c in e]
 return dict(fixture=name,cpu_invocations=sum(len(c['invocations']) for c in e),scenarios=proofs,oracle='unchanged preinspection sealed M10 fixtures and byte interpreter')

def phase(message,owners):
 selected={'unsupported type name':'sema_declarations.c','math builtin vector overloads are outside this increment':'sema_call.c'}
 if message in selected:
  name=selected[message];assert '"'+message+'"' in owners[name]
  return 'semantic_validation',name
 return original_phase(message,owners)
corpus.inventory=inventory;corpus.instruction_cases=instruction_cases;corpus_m8.execute_output=execute_output;corpus_m7.diagnostic_phase=phase
if __name__=='__main__':raise SystemExit(corpus.main())
