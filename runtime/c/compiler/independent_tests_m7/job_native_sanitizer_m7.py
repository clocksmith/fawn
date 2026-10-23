"""One shared sanitized core, two retained/native consumers, compact disk use."""
from __future__ import annotations
import argparse
from pathlib import Path
from native_job_m7 import CFLAGS, Stage, save

HERE=Path(__file__).resolve().parent

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--cc',default='gcc')
    args=parser.parse_args()
    owner=args.source_root.resolve()
    stage=Stage(args.out.resolve(),args.out.resolve()/'build')
    flags=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined']
    include='-I'+str(owner/'include')
    sources=sorted(p for p in (owner/'src').glob('*.c') if not p.name.startswith('cli'))
    report=dict(status='FAIL')
    try:
        library=stage.build/'libjob-sanitizer.so'
        stage.run('build_shared_core',[args.cc,*CFLAGS,*flags,'-shared','-fPIC',include,*sources,'-o',library])
        for name,source in [('job',HERE/'native_job_m7.c'),('retained_m6',HERE/'native_m6.c')]:
            executable=stage.build/name
            stage.run('build_'+name,[args.cc,*CFLAGS,*flags,'-fno-pie','-no-pie',include,source,library,'-Wl,-rpath,'+str(stage.build),'-pthread','-o',executable])
            stage.run('run_'+name,[executable])
        report['status']='PASS'
    except (RuntimeError,ValueError,OSError) as error:
        report['first_failure']=str(error)
    save(stage.output/'sanitizers.json',report)
    return int(report['status']!='PASS')

if __name__=='__main__':
    raise SystemExit(main())
