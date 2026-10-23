"""Storage-only postinspection orchestration; no fixture/oracle changes.

Compress only fresh target modules inside a previously absent owned output root,
after spirv-val exits. Callers retain instruction words in memory. Preserve a
hash/roundtrip/validator-command manifest; never touch a preexisting root or an
external/prebuilt file. Selected M8 originals stay raw for pending mutant reads.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import sys


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--script',type=Path,required=True)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--keep-m8-mutants',action='store_true')
    parser.add_argument('arguments',nargs=argparse.REMAINDER)
    args=parser.parse_args();root=args.root.resolve()
    if root.exists():raise ValueError('compression wrapper requires a fresh owned output root')
    keep=set()
    if args.keep_m8_mutants:
        from mutations_m8 import CASES
        keep={name+'.spv' for kind,name in CASES}
    original_run=subprocess.run;rows=[]
    def observed_run(command, *positional, **kwargs):
        result=original_run(command,*positional,**kwargs)
        if isinstance(command,(list,tuple)) and command and Path(str(command[0])).name=='spirv-val':
            path=Path(str(command[-1])).resolve()
            if path.is_relative_to(root) and path.suffix=='.spv' and path.name not in keep and path.is_file():
                raw=path.read_bytes();sha=hashlib.sha256(raw).hexdigest();compressed=gzip.compress(raw,mtime=0)
                if gzip.decompress(compressed)!=raw:raise RuntimeError('lossless roundtrip failed')
                target=path.with_suffix('.spv.gz')
                if target.exists():raise RuntimeError('will not overwrite existing compressed artifact')
                target.write_bytes(compressed)
                if hashlib.sha256(gzip.decompress(target.read_bytes())).hexdigest()!=sha:raise RuntimeError('persisted roundtrip failed')
                rows.append(dict(original_path=str(path),compressed_path=str(target),original_bytes=len(raw),compressed_bytes=len(compressed),sha256=sha,validator_command=list(map(str,command)),validator_returncode=result.returncode))
                path.unlink() # only the fresh raw duplicate, after completed validation
                (root/'compression_manifest.json').write_text(json.dumps(dict(records=rows,raw_bytes=sum(x['original_bytes'] for x in rows),compressed_bytes=sum(x['compressed_bytes'] for x in rows),decompress='gzip -dc MODULE.spv.gz > MODULE.spv'),indent=2)+'\n')
        return result
    subprocess.run=observed_run
    arguments=args.arguments[1:] if args.arguments[:1]==['--'] else args.arguments
    sys.argv=[str(args.script),*arguments]
    try:runpy.run_path(str(args.script),run_name='__main__')
    except SystemExit as exc:return int(exc.code or 0)
    finally:subprocess.run=original_run
    return 0


if __name__=='__main__':raise SystemExit(main())
