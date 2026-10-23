"""Narrow evidence recheck using exact current M6 adapter and retained libraries.

Normal portable full gate remains run_all_m9_current.py. This only avoids rebuilding
unchanged candidate libraries after the documented historical ceiling correction.
"""
from __future__ import annotations
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
from run_native_m9_final import CFLAGS, ALLOCATORS, digest


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,required=True)
    parser.add_argument('--retained-root',type=Path,required=True)
    parser.add_argument('--native-evidence',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=False)
    here=Path(__file__).resolve().parent
    for name,want in json.loads((here/'CURRENT_SOURCE_MANIFEST.json').read_text())['files'].items():
        assert digest(here/name)==want,name
    source=here/'native_m6_m9_boundary.c';archive=args.native_evidence/'build/libm8-source.a'
    expected=json.loads((args.native_evidence/'native-report.json').read_text())['source_built'][str(archive)]
    assert digest(archive)==expected
    zipped=args.native_evidence/'build/libm8-sanitized.so.gz'
    original=next(row for row in json.loads((args.native_evidence/'compressed-artifacts.json').read_text()) if row['retained']==str(zipped))
    compressed=zipped.read_bytes();raw=gzip.decompress(compressed)
    assert hashlib.sha256(compressed).hexdigest()==original['retained_sha256']
    assert hashlib.sha256(raw).hexdigest()==original['original_sha256']
    fd=os.memfd_create('m9-retained-sanitized-library',0);os.write(fd,raw)
    rows=[];compressed_rows=[]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    try:
        for mode in ('ordinary','sanitized'):
            binary=args.out/mode
            flags=[*CFLAGS,'-pipe','-I'+str(args.source_root/'include'),'-I'+str(args.retained_root/'runtime/c/compiler/independent_tests_m7')]
            if mode=='ordinary':link=[str(archive),'-DWRAP_ALLOCATORS','-Wl,'+','.join('--wrap='+x for x in ALLOCATORS)];passed=()
            else:
                flags+=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie']
                link=[f'/proc/self/fd/{fd}'];passed=(fd,)
            command=['gcc',*flags,str(source),*link,'-pthread','-lm','-o',str(binary)]
            build=subprocess.run(command,capture_output=True,text=True,env=env,pass_fds=passed,timeout=240)
            (args.out/(mode+'-build.log')).write_text(build.stdout+build.stderr)
            row=dict(mode=mode,build_command=command,build_returncode=build.returncode)
            if build.returncode==0:
                run=subprocess.run([str(binary)],capture_output=True,text=True,env=env,pass_fds=passed,timeout=240)
                (args.out/(mode+'-run.log')).write_text(run.stdout+run.stderr)
                row.update(run_returncode=run.returncode,stdout=run.stdout,stderr=run.stderr)
                data=binary.read_bytes();packed=gzip.compress(data,mtime=0);assert gzip.decompress(packed)==data
                target=binary.with_suffix('.gz');target.write_bytes(packed)
                assert gzip.decompress(target.read_bytes())==data
                compressed_rows.append(dict(original=str(binary),original_bytes=len(data),original_sha256=hashlib.sha256(data).hexdigest(),retained=str(target),retained_bytes=len(packed),retained_sha256=digest(target)))
                binary.unlink()
            rows.append(row)
            (args.out/'results.json').write_text(json.dumps(dict(source_sha256=digest(source),archive_sha256=expected,sanitized_library_sha256=original['original_sha256'],rows=rows),indent=2)+'\n')
            (args.out/'compressed-artifacts.json').write_text(json.dumps(compressed_rows,indent=2)+'\n')
            print(mode,'PASS' if row.get('run_returncode')==0 else 'FAIL',row.get('stdout',''),flush=True)
    finally:os.close(fd)
    return any(row.get('run_returncode')!=0 for row in rows)

if __name__=='__main__':raise SystemExit(main())
