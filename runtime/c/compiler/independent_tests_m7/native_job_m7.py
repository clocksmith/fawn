"""Independent M7 native source/prebuilt, allocator, legacy and frame acceptance."""
from __future__ import annotations

import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

from api_m5 import Api, Result, Requirements
from api_job_m7 import Capacities, JobApi, INPUT_ERROR
from evaluate import Evaluator
from fixtures_m6 import program

HERE = Path(__file__).resolve().parent
CFLAGS = ['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Werror']
CPPFLAGS = ['-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Werror','-fno-exceptions','-fno-rtti']
WRAPS = ['malloc','calloc','realloc','free','aligned_alloc','posix_memalign']
EXPORTS = {'doe_compiler_compile','doe_compiler_compile_v2','doe_compiler_workspace_requirements','doe_compiler_job_requirements','doe_compiler_compile_job'}


def save(path: Path, value: object) -> None:
    path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n',encoding='utf-8')


class Stage:
    def __init__(self, output: Path, build: Path) -> None:
        self.output, self.build = output, build
        self.output.mkdir(parents=True,exist_ok=True)
        self.build.mkdir(parents=True,exist_ok=True)
        self.commands = []

    def run(self, name: str, command: list[str | Path]) -> str:
        command = list(map(str,command))
        started = time.monotonic()
        result = subprocess.run(command,capture_output=True,text=True,timeout=180,check=False,
                                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        output = result.stdout+result.stderr
        if len(output)>1000000:
            raise RuntimeError('oversized command log '+name)
        (self.output/(name+'.log')).write_text(output,encoding='utf-8')
        row = dict(name=name,command=command,returncode=result.returncode,elapsed_seconds=time.monotonic()-started)
        self.commands.append(row)
        save(self.output/'commands.json',self.commands)
        if result.returncode:
            if not (self.output/'FIRST_FAILURE.json').exists():
                save(self.output/'FIRST_FAILURE.json',row)
            raise RuntimeError(f'{name}: {result.returncode}; {output[-2500:]}')
        return output


def legacy_boundaries(library: Path, output: Path) -> list[dict]:
    v1 = Evaluator(library,output/'legacy',Path('/nonexistent-not-used'))
    v2 = Api(library)
    job = JobApi(library)
    def tree(leaves: int) -> str:
        if leaves==1:
            return '1u'
        return f'max({tree(leaves//2)},{tree(leaves-leaves//2)})'
    good = [('nodes320','let a='+tree(160)+';let b=a;'),
            ('depth18','let a='+'('*17+'1u'+')'*17+';'),
            ('statements16',''.join(f'let a{i}={i}u;' for i in range(16))),
            ('ifs4','if (1u==1u){'*4+'return;'+'}'*4),
            ('outer_comparison_depth_exempt','if ('+'('*17+'1u'+')'*17+'==1u){return;}'),
            ('outer_comparison_node_exempt','if ('+tree(160)+'==1u){return;}')]
    bad = [('nodes321','let a='+tree(161)+';'),
           ('depth19','let a='+'('*18+'1u'+')'*18+';'),
           ('statements17',''.join(f'let a{i}={i}u;' for i in range(17))),
           ('ifs5','if (1u==1u){'*5+'return;'+'}'*5)]
    rows = []
    for alias in (False,True):
        for admitted, cases in ((True,good),(False,bad)):
            for name, body in cases:
                source,_ = program(('let x=gid.x;' if alias else '')+body,parameters=None if alias else [])
                calls = []
                for label,api in [('ABI1',v1),('ABI2',v2)]:
                    status,result,words,errors = api.call(source)
                    if errors or (admitted and status != 0) or (not admitted and status != INPUT_ERROR):
                        raise AssertionError(f'legacy {label} {name} alias={alias}: status={status}, errors={errors}, diagnostic={bytes(result.diagnostic.message)!r}')
                    calls.append(words)
                    rows.append(dict(name=name,alias=alias,api=label,status=status,word_count=len(words)))
                if admitted and calls[0] != calls[1]:
                    raise AssertionError('ABI1/v2 current words differ '+name)
                # Legacy if comparison exclusions do not alter new job admission.
                js,jr,jw,je = job.call(source)
                if js or je:
                    raise AssertionError(f'expanded job did not admit legacy boundary {name}: {js} {je} {bytes(jr.diagnostic.message)!r}')
                if admitted and jw != calls[0]:
                    raise AssertionError('job/legacy words differ '+name)
    v2.close()
    job.close()
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',type=Path)
    parser.add_argument('--source-root',required=True,type=Path)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--build',type=Path)
    parser.add_argument('--cc',default='gcc')
    parser.add_argument('--cxx',default='g++')
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--skip-sanitizer',action='store_true')
    args = parser.parse_args()
    owner = args.source_root.resolve()
    if not (owner/'src').is_dir():
        owner = owner/'runtime/c/compiler'
    stage = Stage(args.out.resolve(),args.build.resolve() if args.build else args.out.resolve()/'build')
    report = dict(status='FAIL')
    try:
        sources = sorted(path for path in (owner/'src').glob('*.c') if not path.name.startswith('cli'))
        if not sources:
            raise AssertionError('no candidate library C sources')
        save(stage.output/'source-identities.json',{str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in [*sources,*owner.glob('include/*.h'),args.library.resolve(),HERE/'native_job_m7.c',HERE/'native_job_m7.cpp',Path(__file__).resolve()]})
        report['compiler'] = stage.run('compiler_version',[args.cc,'--version']).splitlines()[0]
        report['cxx'] = stage.run('cxx_version',[args.cxx,'--version']).splitlines()[0]
        include = '-I'+str(owner/'include')
        objects = []
        for source in sources:
            obj = stage.build/(source.stem+'.o')
            stage.run('object_'+source.stem,[args.cc,*CFLAGS,'-fPIC','-fstack-usage','-fcallgraph-info=su',include,'-c',source,'-o',obj])
            objects.append(obj)
        frames = []
        for usage in sorted(stage.build.glob('*.su')):
            for line in usage.read_text().splitlines():
                location,size,kind=line.split('\t')
                function=location.split(':')[-1]
                limit=77824 if function=='doe_compiler_compile' else 8192
                frames.append(dict(location=location,function=function,bytes=int(size),qualifier=kind,limit=limit))
        save(stage.output/'stack-frames.json',frames)
        bad=[row for row in frames if row['bytes']>row['limit'] or 'unbounded' in row['qualifier']]
        if bad:
            raise AssertionError('static frame limits: '+str(bad))
        if not any(row['function']=='doe_compiler_compile' for row in frames):
            raise AssertionError('ABI1 frame not measured')
        report['frames']=dict(ABI1=next(row['bytes'] for row in frames if row['function']=='doe_compiler_compile'),other_max=max(row['bytes'] for row in frames if row['function']!='doe_compiler_compile'))
        archive=stage.build/'libjob-source.a'
        stage.run('archive_source',['ar','rcs',archive,*objects])
        linked={}
        for mode,link in [('source',[archive]),('prebuilt',[args.library.resolve(),'-Wl,-rpath,'+str(args.library.resolve().parent)])]:
            for language,compiler,flags,consumer in [('c',args.cc,CFLAGS,HERE/'native_job_m7.c'),('cpp',args.cxx,CPPFLAGS,HERE/'native_job_m7.cpp')]:
                binary=stage.build/(mode+'-'+language)
                interpose=['-DJOB_WRAP_ALLOCATORS','-Wl,'+','.join('--wrap='+name for name in WRAPS)] if mode=='source' and language=='c' else []
                stage.run('build_'+mode+'_'+language,[compiler,*flags,include,consumer,*link,*interpose,'-o',binary])
                linked[mode+'_'+language]=json.loads(stage.run('run_'+mode+'_'+language,[binary]).strip())
        report['consumers']=linked
        for mode, link in [('source', [archive]), ('prebuilt', [args.library.resolve(), '-Wl,-rpath,'+str(args.library.resolve().parent)])]:
            binary=stage.build/('retained-m6-'+mode)
            wrapped=['-DWRAP_ALLOCATORS','-Wl,'+','.join('--wrap='+name for name in WRAPS)] if mode=='source' else []
            stage.run('retained_m6_build_'+mode,[args.cc,*CFLAGS,include,HERE/'native_m6.c',*link,*wrapped,'-pthread','-o',binary])
            stage.run('retained_m6_run_'+mode,[binary])
        cpp=stage.build/'retained-m5-layout'
        stage.run('retained_m5_layout_build',[args.cxx,*CPPFLAGS,include,HERE/'native_layout_m5.cpp',args.library.resolve(),'-Wl,-rpath,'+str(args.library.resolve().parent),'-o',cpp])
        stage.run('retained_m5_layout_run',[cpp])
        expected=dict(sizeof_capacities=C.sizeof(Capacities),alignof_capacities=C.alignment(Capacities),sizeof_result=C.sizeof(Result),sizeof_requirements=C.sizeof(Requirements),offsetof_expression_depth=Capacities.expression_depth.offset)
        for label,layout in linked.items():
            for key,value in expected.items():
                if layout[key]!=value:
                    raise AssertionError(f'C/C++/ctypes ABI layout mismatch {label} {key}')
        exported=stage.run('exported_symbols',['nm','-D','--defined-only',args.library.resolve()])
        symbols={line.split()[-1].split('@')[0] for line in exported.splitlines() if len(line.split())>=3 and line.split()[-2].upper() in {'T','W'}}
        if symbols!=EXPORTS:
            raise AssertionError('export set differs from frozen ABI: '+str(sorted(symbols)))
        imports=stage.run('undefined_source_symbols',['nm','-u',archive])
        banned=set(WRAPS)&{line.split()[-1].split('@')[0] for line in imports.splitlines() if line.split()}
        if banned:
            raise AssertionError('library allocator references: '+str(banned))
        report['legacy_boundaries']=legacy_boundaries(args.library.resolve(),stage.output)
        if not args.skip_sanitizer:
            san=stage.build/'job-sanitizer'
            stage.run('build_sanitizer',[args.cc,*CFLAGS,'-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie',include,*sources,HERE/'native_job_m7.c','-o',san])
            report['sanitizer']=json.loads(stage.run('run_sanitizer',[san]).strip())
            retained_san=stage.build/'retained-m6-sanitizer'
            stage.run('build_retained_sanitizer',[args.cc,*CFLAGS,'-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie',include,*sources,HERE/'native_m6.c','-pthread','-o',retained_san])
            stage.run('run_retained_sanitizer',[retained_san])
        report['status']='PASS'
    except (AssertionError,RuntimeError,OSError,ValueError,subprocess.TimeoutExpired) as error:
        report['first_failure']=str(error)
        print('FAIL:',error)
    save(stage.output/'native-job.json',report)
    return 0 if report['status']=='PASS' else 1


if __name__=='__main__':
    raise SystemExit(main())
