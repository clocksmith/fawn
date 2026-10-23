#!/usr/bin/env python3
"""Materialize only pinned accepted evaluator/source/prebuilt-observation inputs."""
from pathlib import Path
import argparse,subprocess,hashlib,json
COMMIT='dee5f42acbb50e05d5690f1d2d13777c5c26d51d'
p=argparse.ArgumentParser();p.add_argument('--repo',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
names=subprocess.check_output(['git','ls-tree','-r','--name-only',COMMIT],cwd=a.repo,text=True).splitlines();records={}
for n in names:
 if not(n.startswith('runtime/c/compiler/independent_tests') or n.endswith(('.wgsl','.spv')) or n in ['docs/c-migration/corpus-coverage-m5.json','bench/gates/spirv_val_gate.py']):continue
 b=subprocess.check_output(['git','show',COMMIT+':'+n],cwd=a.repo);p=a.out/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b);records[n]=hashlib.sha256(b).hexdigest()
here=a.out/'runtime/c/compiler/independent_tests_m9/source'
for n,h in json.loads((here/'CURRENT_SOURCE_MANIFEST.json').read_text())['files'].items():assert hashlib.sha256((here/n).read_bytes()).hexdigest()==h,n
for n,h in json.loads((here/'IMPORTED_SOURCE.json').read_text()).items():assert records[n]==h,n
(a.out/'PINNED_SOURCE.json').write_text(json.dumps(dict(commit=COMMIT,files=records),sort_keys=True,indent=2)+'\n');print(len(records),'pinned blobs materialized; no current checkout content copied')
