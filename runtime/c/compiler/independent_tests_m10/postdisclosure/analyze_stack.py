from pathlib import Path
import json,re,hashlib,subprocess
import argparse
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);a=p.parse_args();root=a.root.resolve();frames={};edges={};unresolved=set()
for file in root.glob('*.ci'):
 for line in file.read_text().splitlines():
  if line.startswith('node:'):
   name=re.search(r'title: "([^"]+)"',line).group(1);m=re.search(r'\\n(\d+) bytes \(([^)]+)\)',line)
   if m:frames[name]=dict(bytes=int(m[1]),kind=m[2],unit=file.name)
  elif line.startswith('edge:'):
   a,b=re.search(r'sourcename: "([^"]+)" targetname: "([^"]+)"',line).groups();edges.setdefault(a,set()).add(b)
# Kosaraju, explicitly iterative.
seen=set();order=[]
for start in frames:
 if start in seen:continue
 stack=[(start,False)]
 while stack:
  n,done=stack.pop()
  if done:order.append(n);continue
  if n in seen:continue
  seen.add(n);stack.append((n,True));stack.extend((x,False) for x in edges.get(n,()) if x in frames and x not in seen)
rev={n:set() for n in frames}
for a,bs in edges.items():
 for b in bs:
  if a in frames and b in frames:rev[b].add(a)
seen=set();groups=[]
for start in reversed(order):
 if start in seen:continue
 group=[];stack=[start];seen.add(start)
 while stack:
  n=stack.pop();group.append(n)
  for other in rev[n]:
   if other not in seen:seen.add(other);stack.append(other)
 groups.append(group)
cycles=[dict(members=sorted(g),bytes=sum(frames[n]['bytes'] for n in g)) for g in groups if len(g)>1 or g[0] in edges.get(g[0],())]
wrapper=frames['doe_compiler_compile']['bytes'];acyclic=sum(x['bytes'] for n,x in frames.items() if n!='doe_compiler_compile')
report=dict(compiler=subprocess.check_output(['gcc','--version'],text=True).splitlines()[0],frame_count=len(frames),wrapper=wrapper,sum_all_other_frames=acyclic,maximum_nonwrapper=max((x['bytes'],n) for n,x in frames.items() if n!='doe_compiler_compile'),sccs=cycles,fixed_phase_bound=max(512*(18+18+2)+256*(3*4+4),1536*(18+2)+768*(3*4+4),384*(2*4+18+4)),job_phase_bound=max(512*(64+64+2)+256*(3*32+4),1536*(64+2)+768*(3*32+4),384*(2*32+64+4)),frame_qualifiers={n:x for n,x in frames.items() if x['kind']!='static'},frames=frames,ci_sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(root.glob('*.ci'))},su_sha256={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(root.glob('*.su'))})
report['fixed_combined']=wrapper+acyclic+report['fixed_phase_bound'];report['job_combined']=acyclic+report['job_phase_bound']
print(json.dumps({k:v for k,v in report.items() if k not in {'frames','ci_sha256','su_sha256'}},indent=2));(root/'stack-analysis.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
