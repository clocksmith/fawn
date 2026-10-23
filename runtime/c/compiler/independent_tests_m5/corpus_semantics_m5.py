"""Execute each actual unchanged consumer output against source-bound independent vectors."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import all_fixtures
import fixtures_m5
from semantic_checks import run_vectors
from semantic_m5 import execute_vectors
from typed_oracle import Module as StorageModule
from uniform_oracle_m5 import Module as UniformModule

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['repo','corpus','evidence']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.evidence.mkdir(parents=True,exist_ok=False)
    known={}
    for name,source,expected in all_fixtures.admitted(a.repo):known[hashlib.sha256(source).hexdigest()]=(False,name,expected)
    for name,source,expected in fixtures_m5.admitted(a.repo):known[hashlib.sha256(source).hexdigest()]=(True,name,expected)
    records=[]
    for row in json.loads(a.corpus.read_text())['records']:
        if not row['admitted']:continue
        is_uniform,name,expected=known[row['source_sha256']]
        raw=Path(row['output']).read_bytes();assert hashlib.sha256(raw).hexdigest()==row['output_sha256']
        words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));module=(UniformModule if is_uniform else StorageModule)(words);module.interface(expected)
        trials=(execute_vectors if is_uniform else run_vectors)(module,expected)
        records.append(dict(source=row['source'],source_sha256=row['source_sha256'],output_sha256=row['output_sha256'],fixture=name,cpu_trials=trials,passed=True,physical_backend='not_run',runtime_adoption='not_run'))
    report=dict(checks=len(records),passed=len(records),cpu_trials=sum(r['cpu_trials'] for r in records),records=records)
    (a.evidence/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='records'}));return 0
if __name__=='__main__':raise SystemExit(main())
