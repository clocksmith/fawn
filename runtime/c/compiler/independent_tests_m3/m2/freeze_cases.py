"""Write reviewed black-box sources and exact inputs/expected outputs as evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import fixtures
import fixtures_m1
import m2_fixtures

def serial(value):
    if isinstance(value,dict):
        return {str(k):serial(v) for k,v in value.items()}
    if isinstance(value,(list,tuple)):
        return [serial(v) for v in value]
    return value

p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=False)
records=[]
for kind,cases in [('positive',fixtures.admitted(a.root)),('negative',fixtures.rejected())]:
    for case in cases:
        name,source,*expect=case
        dest=a.out/(kind+'_'+name+'.wgsl');dest.write_bytes(source)
        records.append(dict(kind=kind,name=name,source=dest.name,sha256=hashlib.sha256(source).hexdigest(),expected=serial(expect)))
lifts=[]
for name,source,off,cat in fixtures_m1.rejected():
    if name in m2_fixtures.LIFTS:
        lifts.append(dict(name=name,source=source.decode(),sha256=hashlib.sha256(source).hexdigest(),reason=m2_fixtures.LIFTS[name]))
(a.out/'cases.json').write_text(json.dumps(records,indent=2)+'\n')
(a.out/'m1-rejection-lifts.json').write_text(json.dumps(lifts,indent=2)+'\n')
print(len(records),'frozen cases;',len(lifts),'explicit M1 lifts')
