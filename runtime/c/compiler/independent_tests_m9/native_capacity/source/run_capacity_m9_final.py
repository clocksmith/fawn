"""Unchanged sealed capacity fixtures; corrected validator-resource routing only."""
from __future__ import annotations
import hashlib
import struct
import subprocess
from pathlib import Path
import capacity_m9 as frozen


def validate(words: list[int],validator: Path,large: bool) -> dict:
    del large
    at=5;max_formals=0
    while at<len(words):
        width=words[at]>>16
        if width==0:raise ValueError('invalid module framing')
        if words[at]&65535==33:max_formals=max(max_formals,width-3)
        at+=width
    binary=struct.pack('<'+'I'*len(words),*words)
    command=[str(validator),'--target-env','vulkan1.1']
    if max_formals>255:command+=['--max-function-args','65532']
    run=subprocess.run(command,input=binary,capture_output=True,timeout=1800 if max_formals>255 else 120)
    return dict(command=command,returncode=run.returncode,stderr=run.stderr.decode(errors='replace'),
                default_returncode=run.returncode if max_formals<=255 else None,
                default_stderr='' if max_formals<=255 else 'Not repeated: default255 argument resource ceiling and original timeout retained separately.',
                sha256=hashlib.sha256(binary).hexdigest(),words=len(words))

frozen.validate=validate
if __name__=='__main__':raise SystemExit(frozen.main())
