"""Post-black-box source-review-derived maximum capacity regression, separately labelled."""
import argparse
from pathlib import Path
import json
import m2_fixtures as fixtures
from fixtures_m1 import binding
from evaluate import Evaluator

p=argparse.ArgumentParser();p.add_argument('--library',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
names=['r'+str(i)+'x'*61 for i in range(8)]
bs=[binding(name,i//2,i%2,1,2) for i,name in enumerate(names)]
# Three literal-base-plus-literal index operations require the reviewed maximum
# 13 lowered instructions per assignment, 209 including invocation.
body=(f'{names[0]}[4294967295u+1u] = {names[1]}[0u+1u] + {names[2]}[2u-1u];\n')*16
source,expect=fixtures.program(body,bindings=bs,entry='e'*63,parameter='p'*63,alias=False)
initial={(i//2,i%2):[31,3] if i==1 else [97,5] if i==2 else [fixtures.S,fixtures.S] for i in range(8)}
want={k:list(v) for k,v in initial.items()};want[(0,0)]=[8,fixtures.S]
expect['vectors']=[dict(x=x,initial=initial,want=want) for x in [0,1,fixtures.U]]
e=Evaluator(a.library,a.out,a.validator);e.valid('source_review_maximum_209_ir',source,expect);e.boundary(source)
(a.out/'maximum.wgsl').write_bytes(source)
report=dict(checks=len(e.records),passed=sum(r['passed'] for r in e.records),failed=[r for r in e.records if not r['passed']],records=e.records,cpu_trials=e.cpu_trials)
(a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2));raise SystemExit(int(bool(report['failed'])))
