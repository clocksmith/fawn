"""Storage-only wrapper preserving every fresh artifact byte under a small disk slot.

No test expectation is changed. Inputs and old receipts are never removed.
Standalone build executables are gzip-retained after their sole run. CLI fixture
folders are packed only when the next folder starts, after all prior reads.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

HERE=Path(__file__).resolve().parent


def digest(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()


class Retain:
    def __init__(self, output: Path) -> None:
        self.output=output; self.records=[]

    def save(self) -> None:
        self.output.mkdir(parents=True,exist_ok=True)
        (self.output/'compressed-artifacts.json').write_text(json.dumps(self.records,indent=2,sort_keys=True)+'\n')

    def file(self, path: Path) -> None:
        if not path.is_file() or path.suffix=='.gz': return
        expected=digest(path); zipped=path.with_name(path.name+'.gz')
        if zipped.exists(): raise RuntimeError('refusing to replace artifact: '+str(zipped))
        with path.open('rb') as src, zipped.open('wb') as target:
            with gzip.GzipFile(fileobj=target,mode='wb',mtime=0) as output:
                shutil.copyfileobj(src,output)
        h=hashlib.sha256()
        with gzip.open(zipped,'rb') as src:
            for chunk in iter(lambda:src.read(1024*1024),b''): h.update(chunk)
        if h.hexdigest()!=expected: raise RuntimeError('gzip roundtrip changed '+str(path))
        self.records.append(dict(original=str(path),original_bytes=path.stat().st_size,
                                 original_sha256=expected,retained=str(zipped),
                                 retained_bytes=zipped.stat().st_size,retained_sha256=digest(zipped)))
        self.save(); path.unlink()  # Only the just-verified fresh disposable copy.

    def folder(self, path: Path | None) -> None:
        if path is None or not path.is_dir(): return
        zipped=path.with_name(path.name+'.tar.gz')
        if zipped.exists(): raise RuntimeError('refusing to replace artifact: '+str(zipped))
        files={str(p.relative_to(path)):digest(p) for p in path.rglob('*') if p.is_file() and not p.is_symlink()}
        with tarfile.open(zipped,'w:gz') as archive: archive.add(path,arcname=path.name)
        with tarfile.open(zipped,'r:gz') as archive:
            for relative,expected in files.items():
                member=archive.extractfile(path.name+'/'+relative)
                if member is None or hashlib.sha256(member.read()).hexdigest()!=expected:
                    raise RuntimeError('tar roundtrip changed '+str(path/relative))
        self.records.append(dict(original=str(path),files=files,retained=str(zipped),retained_sha256=digest(zipped)))
        self.save(); shutil.rmtree(path)  # Owned fresh disposable tree only, archive verified.


def main() -> int:
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('stage',choices=('native','cli'))
    known,remaining=p.parse_known_args()
    out_index=remaining.index('--out')+1
    output=Path(remaining[out_index]).resolve(); retain=Retain(output)
    # Source/import manifest checks remain in the original reviewed stage.
    if known.stage=='native':
        import run_native_m9_reviewed as runner
        original=runner.Stage.run
        def run(self,name,command,*args,**kwargs):
            if name=='build_sanitized_core':
                for path in sorted(self.build.iterdir()):
                    if path.suffix in ('.o','.a','.ci','.su'): retain.file(path)
            result=original(self,name,command,*args,**kwargs)
            executed=Path(str(command[0]))
            if executed.parent==self.build and executed.is_file() and executed.suffix=='':
                retain.file(executed)
            return result
        runner.Stage.run=run
        sys.argv=[str(HERE/'run_native_m9_reviewed.py'),*remaining]
        status=runner.main()
        for path in sorted((output/'build').iterdir()):
            if path.name!='libm8-source.so' and path.suffix!='.gz': retain.file(path)
    else:
        import run_cli_numeric_m9 as runner
        original=runner.Checks.directory; active=[None]
        def directory(self,name):
            retain.folder(active[0]); active[0]=original(self,name); return active[0]
        runner.Checks.directory=directory
        sys.argv=[str(HERE/'run_cli_numeric_m9.py'),*remaining]
        status=runner.main(); retain.folder(active[0])
        for path in sorted((output/'build_receipts/build').iterdir()): retain.file(path)
    retain.save()
    return status

if __name__=='__main__': raise SystemExit(main())
