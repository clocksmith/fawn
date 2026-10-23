from pathlib import Path
import hashlib,json,struct
ROOT=Path('/tmp/doe-m11-independent-target-LPWtOk');OUT=Path('/workspace/shared/doe-c-migration-evidence-20261023/milestone11/evaluator/native');FROZEN=OUT.parent/'frozen'
TYPE=set(range(19,34));UNARY={109,110,111,112,113,114,115,124,126,127,156,157,158,159,160,168,200};BINARY={142}|set(range(128,142))|set(range(161,168))|set(range(170,192))|set(range(194,200));RESULT={12,41,42,43,44,46,54,55,57,59,61,65,66,68,77,78,79,80,81,82,83,169,245}|UNARY|BINARY
rows=[]
for source in sorted(FROZEN.glob('witness*.wgsl')):
 candidates=[ROOT/'cases'/(source.stem+'.spv')]+list(ROOT.glob('early-*/'+source.stem+'.spv'));path=next((p for p in candidates if p.exists()),None);assert path,source
 data=path.read_bytes();assert len(data)%4==0;w=struct.unpack('<'+'I'*(len(data)//4),data);ids=set();ops=[];n=5
 while n<len(w):
  size,op=w[n]>>16,w[n]&65535;assert size and n+size<=len(w);a=w[n+1:n+size];rid=a[0] if op in TYPE|{11,248} else a[1] if op in RESULT else None
  if rid is not None:assert rid not in ids and rid<w[3];ids.add(rid)
  if op==224:assert size==4
  if op==59 and a[2]==4:assert size==4
  if op==46:assert size==3
  if op==32:assert size==4
  ops.append((op,a));n+=size
 assert max(ids)+1==w[3],(source.name,max(ids),w[3]);roots=[a[1] for op,a in ops if op==59 and a[2]==4];zeros={a[1] for op,a in ops if op==46};stores=[a for op,a in ops if op==62 and a[0] in roots and a[1] in zeros];assert len(stores)==len(roots)
 rows.append({'case':source.stem,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'path':str(path),'spv_sha256':hashlib.sha256(data).hexdigest(),'words':len(w),'bound':w[3],'highest_defined_id':max(ids),'workgroup_roots':len(roots),'whole_null_stores':len(stores),'static_barriers':sum(op==224 for op,a in ops)})
(OUT/'candidate2-TARGET_COUNTS.json').write_text(json.dumps({'status':'PASS','cases':rows,'scope':'Reuses parent fresh emitted bytes; actual highest-ID+1 and exact Workgroup/null/pointer/barrier row widths, no additional compile/execution claim'},indent=2)+'\n');print('PASS',len(rows),'source-bound witness target ID/count audits')
