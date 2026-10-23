#!/usr/bin/env python3
"""Independent exact slice arithmetic and complete fresh GCC frame audit."""
from pathlib import Path
import json,re,hashlib,subprocess
HERE=Path(__file__).resolve().parent;BUILD=Path('/tmp/doe-m11-independent-native-build2');OWNER=Path('/tmp/doe-m11-independent-native-candidate2/runtime/c/compiler')
expected={'CompilerWorkspace':(552,8),'Expression':(40,4),'Statement':(56,4),'ConstantSyntax':(40,8),'FunctionRecord':(80,4),'FormalRecord':(16,4),'ArgumentSyntax':(8,4),'GenericSyntax':(24,4),'TypeRecord':(56,8),'MemberRecord':(56,8),'ResourceRecord':(104,8),'WorkgroupRoot':(24,4),'ConstantRecord':(32,8),'ConstantValue':(16,8),'AccessStep':(24,4),'IrInstruction':(32,4),'IrLocal':(8,4),'LocalBinding':(48,8),'TargetDeclaration':(32,8),'WalkItem':(16,4),'Token':(12,4),'LegacyWorkspace':(146256,8),'UniformityFrame':(32,4)}
records={};slices={};limits={};public={}
for line in (HERE/'candidate2-layout.log').read_text().splitlines():
 r=line.split()
 if r[0]=='RECORD':records[r[1]]=tuple(map(int,r[2:]))
 if r[0]=='SLICE':slices.setdefault(r[1],{})[r[2]]=int(r[3])
 if r[0]=='LIMITS':limits[r[1]]=dict(zip('T E S I Q M R X K L O D V W PE TS CE CC'.split(),map(int,r[2:])))
 if r[0]=='PUBLIC':public[r[1]]=tuple(map(int,r[2:]))
assert records==expected,(records,expected)
def align(n,a):return ((n+a-1)//a)*a
def layout(l):
 T,E,S,I,Q,M,R,X,K,L,O,D,V,W,PE,TS,CE,CC=(l[k] for k in 'T E S I Q M R X K L O D V W PE TS CE CC'.split());F=T//6;P=T//4;G=T//8;n=552;out={}
 for field,typ,count in [('expressions','Expression',PE),('statements','Statement',S),('declarations','ConstantSyntax',E+1),('functions','FunctionRecord',F),('formals','FormalRecord',P),('arguments','ArgumentSyntax',E),('callees','U32',PE),('generic_syntax','GenericSyntax',TS),('types','TypeRecord',Q),('members','MemberRecord',M),('resources','ResourceRecord',R),('workgroups','WorkgroupRoot',G),('constants','ConstantRecord',K),('edges','ConstantValue',L),('accesses','AccessStep',X)]:
  width,a=(4,4) if typ=='U32' else expected[typ];n=align(n,a);out[field]=n;n+=width*count
 n=align(n,8);out['phase']=n;persistent=n;token=12*(T+1);n+=32*I
 for field,count,width,a in [('locals',S+1+X,8,4),('scope',S+1,48,8),('values',I+P+1,4,4),('operands',O,4,4),('behaviors',S,1,1)]:n=align(n,a);out[field]=n;n+=count*width
 n=align(n,8);out['uniformity']=n;common=n-persistent
 for field,count,width,a in [('reflection',Q+7,4,4),('targets',D,32,8),('walk',V,16,4),('words',W,4,4)]:n=align(n,a);out[field]=n;n+=count*width
 target=align(n-out['uniformity'],8);H=2*CC+CE+4;uniform=align(align(PE+S+1+P+F,4)+H*32+H*6*(S+1),8);out['total']=persistent+max(token,common+max(target,uniform))
 return out,{'persistent':persistent,'tokens':token,'common':common,'target':target,'uniformity':uniform,'total':out['total'],'H':H,'G':G}
profiles={}
for name,l in limits.items():
 predicted,detail=layout(l);assert predicted==slices[name],(name,predicted,slices[name]);assert public[name][0]==predicted['total'];assert public[name][1]>=8 and public[name][1]%8==0
 profiles[name]=dict(detail,actual_slices=slices[name],public_alignment=public[name][1],private_alignment=8)
expected_sizes={'fixed':(92456,24588,20856,32944,5232,146256),'typed_cli':(995880,98316,1008744,531504,428912,2536128),'job_cli':(735336,98316,1008744,278704,428912,2172992)}
for n,v in expected_sizes.items():assert tuple(profiles[n][k] for k in ['persistent','tokens','common','target','uniformity','total'])==v
# The retained analyzer is copied only as an input parser. Its old family dispatch
# is not used; new SCCs and complete coverage are checked explicitly below.
stack=json.loads((BUILD/'stack-analysis.json').read_text());frames=stack['frames'];limits_scc={};family_seen=set()
for scc in stack['sccs']:
 names=' '.join(scc['members'])
 if 'uniformity.c:expression_bit' in names:family,limit='uniformity_expression',256
 elif 'uniformity.c:block_bits' in names:family,limit='uniformity_control',256
 elif 'parse.c:expression' in names:family,limit='parser_expression_type',512
 elif 'parse.c:statement' in names:family,limit='parser_control',256
 elif 'validate.c:lower_block' in names:family,limit='sema_control',768
 elif 'spirv.c:emit_block' in names:family,limit='emitter',384
 elif 'sema_expression' in names:family,limit='sema_expression_type',1536
 else:raise AssertionError('Unclassified recursive family '+names)
 assert scc['bytes']<=limit;limits_scc[family]={'actual':scc['bytes'],'ceiling':limit,'members':scc['members']};family_seen.add(family)
assert len(family_seen)==7
edges={};external=set();su=[]
for path in BUILD.glob('*.ci'):
 for line in path.read_text().splitlines():
  if line.startswith('edge:'):
   a,b=re.search(r'sourcename: "([^"]+)" targetname: "([^"]+)"',line).groups();edges.setdefault(a,set()).add(b)
   if b not in frames:external.add(b)
aliases={}
for unit in BUILD.glob('*.o'):
 symbols={}
 for line in subprocess.check_output(['nm','-a',str(unit)],text=True).splitlines():
  r=line.split()
  if len(r)==3 and r[1].lower()=='t':symbols[r[2]]=r[0]
 for alias,address in symbols.items():
  if alias.endswith('.localalias'):
   canonical=alias.removesuffix('.localalias');assert symbols.get(canonical)==address
   for target in list(external):
    if target.endswith(':'+alias):aliases[target]=canonical
for a,bs in edges.items():edges[a]={aliases.get(b,b) for b in bs}
external={b for bs in edges.values() for b in bs if b not in frames}
# Alias-normalized graph must preserve the measured recursive SCCs and contain
# no undisclosed compiler-owned leaves. Every optimized alias is address-proved.
seen=set();order=[]
for start in frames:
 if start in seen:continue
 todo=[(start,False)]
 while todo:
  n,done=todo.pop()
  if done:order.append(n);continue
  if n in seen:continue
  seen.add(n);todo.append((n,True));todo.extend((x,False) for x in edges.get(n,()) if x in frames and x not in seen)
reverse={n:set() for n in frames}
for n,bs in edges.items():
 for b in bs:
  if b in frames:reverse[b].add(n)
seen=set();cycles=[]
for start in reversed(order):
 if start in seen:continue
 group=[];todo=[start];seen.add(start)
 while todo:
  n=todo.pop();group.append(n)
  for other in reverse[n]:
   if other not in seen:seen.add(other);todo.append(other)
 if len(group)>1 or start in edges.get(start,()):cycles.append(frozenset(group))
assert set(cycles)=={frozenset(v['members']) for v in stack['sccs']}
for path in BUILD.glob('*.su'):
 for line in path.read_text().splitlines():
  loc,size,kind=line.split('\t');su.append((path.stem,loc.rsplit(':',1)[-1],int(size),kind));assert 'unbounded' not in kind
assert len(su)==len(frames)
def clone_name(n):return re.sub(r'\.(isra|constprop)\.\d+',r'.\1',n.rsplit(':',1)[-1])
for unit,name,size,kind in su:assert any(v['unit']==unit+'.ci' and v['bytes']==size and clone_name(k)==clone_name(name) for k,v in frames.items()),(unit,name)
compiler_symbols=set()
for obj in BUILD.glob('*.o'):
 for line in subprocess.check_output(['nm','--defined-only',str(obj)],text=True).splitlines():
  r=line.split()
  if len(r)>=3 and r[-2]=='T':compiler_symbols.add(r[-1])
assert not (external&compiler_symbols),external&compiler_symbols
assert stack['wrapper']<=147968 and stack['sum_all_other_frames']<=16384 and stack['maximum_nonwrapper'][0]<=8192
for name in ['doe_compiler_compile_v2','doe_compiler_compile_job','doe_compiler_compile_typed']:assert frames[name]['bytes']<=1024
assert all(v['kind'] in ['static','dynamic,bounded'] for v in frames.values())
uniformity_fixed=256*(18+2)+256*(3*4+4);uniformity_job=256*(64+2)+256*(3*32+4)
assert uniformity_fixed==9216 and uniformity_job==42496
assert stack['fixed_phase_bound']>=uniformity_fixed and stack['job_phase_bound']>=uniformity_job
assert stack['fixed_combined']<=212992 and stack['job_combined']<=196608
report={'status':'PASS','source_manifest_sha256':'b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af','records':records,'profiles':profiles,'stack':stack,'recursive_families':limits_scc,'uniformity_phase_bounds':{'fixed':uniformity_fixed,'job':uniformity_job},'callgraph_edges':{n:sorted(v) for n,v in edges.items()},'external_calls_excluded':sorted(external),'address_proved_local_aliases':aliases,'su_ci_exact_coverage':len(su),'scope':'GCC14.2 C-owned stack bounds; conservative all-non-wrapper frame sum includes even mutually exclusive and recursive functions once, plus retained recurrence bounds. External libc/libm/OS/sanitizer/caller frames excluded, not observed peaks.'}
(HERE/'candidate2-LEDGER_STACK.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n');print(json.dumps({'status':'PASS','profiles':profiles,'fixed_combined':stack['fixed_combined'],'job_combined':stack['job_combined'],'SCC':{n:v['actual'] for n,v in limits_scc.items()}},indent=2))
