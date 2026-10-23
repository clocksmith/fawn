"""Narrow postinspection validator-resource corrections; fixtures unchanged.

Original512/2048default-ceiling failures and65531-call default-validation timeout
remain in candidate1-capacity. This runner uses the previously sealed argument
resource setting for the three affected exact sources. No semantic flags change.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
import subprocess
import time
from pathlib import Path
from capacity_m9 import fixtures,format_fixture,inspect_format,JobApi,Capacities


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--library',required=True,type=Path)
    p.add_argument('--validator',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    rows=[]
    for row in [r for r in fixtures() if r['name'] in ('zero_es_formals_512','zero_es_formals_2048')]+[format_fixture(65531,True)]:
        api=JobApi(a.library,Capacities(**row['capacities']))
        source=row['source'].encode();status,result,words,errors=api.call(source);api.close()
        record=dict(name=row['name'],source_sha256=hashlib.sha256(source).hexdigest(),capacities=row['capacities'],status=status,errors=errors)
        if status!=0:record['errors'].append('original positive compile no longer accepted')
        else:
            record['errors']+=inspect_format(words,row)
            binary=struct.pack('<'+'I'*len(words),*words)
            record.update(words=len(words),module_sha256=hashlib.sha256(binary).hexdigest())
            command=[str(a.validator),'--target-env','vulkan1.1','--max-function-args','65532']
            print('VALIDATING',row['name'],len(words),'words',flush=True);started=time.monotonic()
            run=subprocess.run(command,input=binary,capture_output=True,timeout=1800)
            record['validation']=dict(command=command,returncode=run.returncode,stderr=run.stderr.decode(errors='replace'),elapsed_seconds=time.monotonic()-started)
            if run.returncode:record['errors'].append('required configured Vulkan1.1 validator rejected')
        rows.append(record);(a.out/'results.json').write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
        print('FAIL' if record['errors'] else 'PASS',row['name'],record['errors'],flush=True)
    return any(row['errors'] for row in rows)

if __name__=='__main__':raise SystemExit(main())
