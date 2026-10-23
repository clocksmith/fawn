"""Post-observation additive static-lowering controls derived only from frozen ledger."""
from pathlib import Path
import json,hashlib
ROOT=Path(__file__).resolve().parent
rows=[]
for name in ['min','max','clamp','exp','tanh','sqrt','dot','length','distance','normalize','cross']:
 leaves=['i32','u32','f32'] if name in ['min','max','clamp','dot'] else ['f32']
 widths=[3] if name=='cross' else range(2,5) if name in ['dot','normalize'] else range(1,5)
 arity=3 if name=='clamp' else 2 if name in ['min','max','dot','distance','cross'] else 1
 for leaf in leaves:
  for n in widths:
   typ=leaf if n==1 else f'vec{n}<{leaf}>';ret=leaf if name in ['dot','length','distance'] else typ
   source='fn probe('+','.join(f'{x}:{typ}' for x in 'abc'[:arity])+f') -> {ret} '+'{return '+name+'('+','.join('abc'[:arity])+');}\n@compute @workgroup_size(1) fn main(){}\n'
   if name in ['min','max']:
    ids,words=(2,11) if n==1 else (4*n+1,22*n+3);ops={169:n,81:0 if n==1 else 2*n,80:0 if n==1 else 1};ops[184 if leaf=='f32' else 177 if leaf=='i32' else 176]=n
   elif name=='clamp':
    ids,words=(4,22) if n==1 else (7*n+1,38*n+3);ops={169:2*n,81:0 if n==1 else 3*n,80:0 if n==1 else 1};ops[184 if leaf=='f32' else 177 if leaf=='i32' else 176]=2*n
   elif name=='dot':
    ids,words=(1,5) if leaf=='f32' else (2*n,10*n);ops={148:1} if leaf=='f32' else {132:1,81:n,128:n-1}
   else:ids,words=1,7 if name in ['distance','cross'] else 6;ops={12:1}
   ident=f'{name}_{leaf}_{n}';path=ROOT/'lowering_ledger'/f'{ident}.wgsl';path.parent.mkdir(exist_ok=True);path.write_text(source)
   rows.append({'id':ident,'source':str(path.relative_to(ROOT)),'sha256':hashlib.sha256(source.encode()).hexdigest(),'body_result_ids':ids,'body_words':words,'body_opcodes':{str(k):v for k,v in ops.items() if v},'uses_extended_import':name not in ['min','max','clamp','dot']})
assert len(rows)==69
(ROOT/'LOWERING_LEDGER.json').write_text(json.dumps({'phase':'Post-observation additive source-independent ledger audit; not pre-candidate','contract':'MILESTONE_12_LEDGER.md exact target rows','cases':rows},indent=2,sort_keys=True)+'\n')
paths=[ROOT/'prepare_lowering_ledger.py',ROOT/'LOWERING_LEDGER.json',ROOT/'run_lowering_ledger.py']+[ROOT/r['source'] for r in rows]
(ROOT/'LOWERING_LEDGER.sha256').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(ROOT))+'\n' for p in paths));print(hashlib.sha256((ROOT/'LOWERING_LEDGER.sha256').read_bytes()).hexdigest())
