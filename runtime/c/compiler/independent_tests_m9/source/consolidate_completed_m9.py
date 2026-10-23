"""Consolidate explicitly disposable completed binary intermediates only."""
import argparse,gzip,hashlib,io,json,tarfile
from pathlib import Path

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);a=p.parse_args();root=a.root.resolve()
 if root.name not in {'candidate1-m8','candidate1-historical'}:raise ValueError('not explicitly authorized completed output root')
 if not (root/'results.json').is_file():raise ValueError('no completed receipt')
 files=sorted(x for x in (root/'modules').iterdir() if x.name.endswith(('.spv','.spv.gz')))
 archive=root/'completed-module-intermediates.tar.gz';manifest=root/'completed-module-intermediates.json'
 if archive.exists() or manifest.exists():raise ValueError('do not overwrite existing archive/receipt')
 rows=[dict(path=str(f.relative_to(root)),bytes=f.stat().st_size,allocated_bytes=f.stat().st_blocks*512,sha256=hashlib.sha256(f.read_bytes()).hexdigest()) for f in files]
 with archive.open('wb') as out:
  with gzip.GzipFile(filename='',fileobj=out,mode='wb',mtime=0) as gz:
   with tarfile.open(fileobj=gz,mode='w') as tar:
    for f,row in zip(files,rows):
     data=f.read_bytes();info=tarfile.TarInfo(row['path']);info.size=len(data);info.mode=0o644;info.mtime=0;tar.addfile(info,io.BytesIO(data))
 with tarfile.open(archive,'r:gz') as tar:
  assert tar.getnames()==[r['path'] for r in rows]
  for member,row in zip(tar.getmembers(),rows):
   data=tar.extractfile(member).read();assert len(data)==row['bytes'] and hashlib.sha256(data).hexdigest()==row['sha256']
 report=dict(records=rows,archive=archive.name,archive_sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),archive_bytes=archive.stat().st_size,archive_allocated_bytes=archive.stat().st_blocks*512,verified_extraction=True,raw_duplicate_allocated_bytes=sum(r['allocated_bytes'] for r in rows),binary_allocated_bytes_reclaimed=sum(r['allocated_bytes'] for r in rows)-archive.stat().st_blocks*512,restore='tar -xzf completed-module-intermediates.tar.gz (from this receipt directory); then verify every path SHA256; gzip -dc any .spv.gz to recover SPIR-V')
 manifest.write_text(json.dumps(report,indent=2)+'\n')
 # No pending consumer: all corresponding compile/validate/execute/mutation
 # commands have exited and their result receipts are retained.
 for f,row in zip(files,rows):
  assert hashlib.sha256(f.read_bytes()).hexdigest()==row['sha256'];f.unlink()
 print(json.dumps({k:v for k,v in report.items() if k!='records'},indent=2))

if __name__=='__main__':main()
