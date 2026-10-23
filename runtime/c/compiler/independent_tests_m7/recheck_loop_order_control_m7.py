"""Valid-target loop-order mutation using already compiled candidate output."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from compute_oracle_m7 import Module
from fixtures_m7 import positives
from semantic_m7 import execute_vectors
from spirv_oracle_base import ExecutionError

def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    for name in ['module','validator','out']:p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    raw=a.module.read_bytes();words=list(struct.unpack('<'+'I'*(len(raw)//4),raw));module=Module(words)
    expected=next(e for n,s,e in positives() if n=='for_continue_updates')
    module.interface(expected);execute_vectors(module,expected)
    loop_i=next(i for i,(op,args,o) in enumerate(module.instructions) if op==246)
    continuing=module.instructions[loop_i][1][1]
    branch=next(args for op,args,o in module.instructions[loop_i+1:] if op==250)
    body=branch[1];labels={args[0]:o for op,args,o in module.instructions if op==248}
    begin=labels[continuing]+2;end=next(o for op,args,o in module.instructions if o>=begin and op in {249,250,253})
    moved=words[begin:end];del words[begin:end];insert=labels[body]+2
    if insert>begin:insert-=end-begin
    words[insert:insert]=moved
    path=a.out/'update_before_body.spv';path.write_bytes(struct.pack('<'+'I'*len(words),*words))
    command=[str(a.validator),'--target-env','vulkan1.1',str(path)]
    val=subprocess.run(command,capture_output=True,text=True)
    record=dict(source_output_sha256=hashlib.sha256(raw).hexdigest(),mutation_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),validator_command=command,validator_status=val.returncode,validator_stderr=val.stderr,detected=False)
    if val.returncode==0:
        try:
            bad=Module(words);bad.interface(expected);execute_vectors(bad,expected)
        except ExecutionError as error:record.update(detected=True,detection='instruction_execution',reason=str(error))
    (a.out/'results.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2));return int(not record['detected'])

if __name__=='__main__':raise SystemExit(main())
