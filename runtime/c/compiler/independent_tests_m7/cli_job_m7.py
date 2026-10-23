"""Independent M7 CLI exact job routing, explicit lifts and acquisition rollback."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

import cli_m5
from api_job_m7 import DEFAULT_CAPACITIES, STORE
from fixtures_m6 import program
from native_job_m7 import CFLAGS, Stage, save

HERE = Path(__file__).resolve().parent
WRAPS = [*cli_m5.WRAPS, 'open', 'doe_compiler_job_requirements', 'doe_compiler_compile_job']
VALIDATOR = Path('/workspace/shared/doe-language-experiment-20261023/phase2/deps/spirv-tools/usr/bin/spirv-val')


class Checks(cli_m5.CheckCli):
    def record(self, case: str, errors: list[str], **details: object) -> None:
        super().record(case, errors, **details)
        if errors:
            first=self.args.evidence/'FIRST_FAILURE.json'
            if not first.exists():
                save(first,self.records[-1])

    def invoke(self, cli: Path, parts: list[str | Path], directory: Path,
               operation: str | None = None, call: int = 1) -> tuple:
        command=[str(cli),*map(str,parts)]
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        env.pop('M7_FAULT_OPERATION',None)
        env.pop('M7_FAULT_CALL',None)
        if operation:
            env.update(M7_FAULT_OPERATION=operation,M7_FAULT_CALL=str(call))
        run=subprocess.run(command,capture_output=True,text=True,timeout=60,cwd=directory,env=env,check=False)
        trace=None
        for line in run.stderr.splitlines():
            if line.startswith('M7_TRACE '):
                trace=json.loads(line[len('M7_TRACE '):])
        self.commands.append(dict(command=command,cwd=str(directory),fault_operation=operation,fault_call=call,returncode=run.returncode,stdout=run.stdout,stderr=run.stderr))
        return run,trace

    def capability_lifts(self, cli: Path) -> None:
        def tree(leaves: int) -> str:
            if leaves==1:
                return '1u'
            return '('+tree(leaves//2)+'+'+tree(leaves-leaves//2)+')'
        # All changes to old rejections are specific declared job capabilities.
        cases=[('mutable_for','var x:u32;for(var i:u32=0u;i<4u;i++){x+=i;}return;'),
               ('statements17',''.join(f'let x{i}={i}u;' for i in range(17))),
               ('nodes321','let x='+tree(161)+';'),
               ('expression_depth19','let x='+'('*18+'1u'+')'*18+';'),
               ('control_depth5','if (1u==1u){'*5+'return;'+'}'*5),
               ('tokens_above2048','let x='+tree(600)+';'),
               ('words_above4096',''.join('out[0u]=1u;' for _ in range(120)))]
        for name,body in cases:
            if name=='words_above4096':
                source=STORE[:STORE.index(b'{')+1]+body.encode()+b'}'
            else:
                source,_=program(body,parameters=[])
            words=None
            for mode in cli_m5.MODES:
                d=self.directory('lift_'+name+'_'+mode)
                (d/'source.wgsl').write_bytes(source)
                before=cli_m5.tree(d)
                run,_=self.invoke(cli,self.parts(d,mode),d)
                errors=[]
                if mode=='legacy':
                    if run.returncode==0 or cli_m5.tree(d)!=before:
                        errors.append('ABI1 capability boundary broadened or failure published')
                elif run.returncode:
                    errors.append('Explicit job capability lift failed: '+run.stderr)
                else:
                    errors+=self.validate(d/'module.spv')
                    current=(d/'module.spv').read_bytes()
                    if words is not None and current!=words:
                        errors.append('Binary/v2 job words differ')
                    words=current
                    if name=='words_above4096' and len(current)<=4096*4:
                        errors.append('Fixture does not establish expanded word capacity')
                self.record('lift_'+name+'_'+mode,errors,capability_lift=name,returncode=run.returncode,stderr=run.stderr,source_sha256=cli_m5.sha(source))
        exact=[('statement1024',';'*1024,True),('statement1025',';'*1025,False),
               ('expression_depth32','let x='+'('*31+'1u'+')'*31+';',True),
               ('expression_depth33','let x='+'('*32+'1u'+')'*32+';',False),
               ('control_depth16','{'*16+'}'*16,True),('control_depth17','{'*17+'}'*17,False)]
        # The scalar budget can be reached without reaching token/depth limits.
        body=''.join(f'let a{i}='+('+'.join(['1u']*30))+';' for i in range(69))
        body+='let last='+('+'.join(['1u']*13))+';'
        exact += [('expressions4096',body,True),('expressions4097',body+'let z=last;',False)]
        for name,body,admitted in exact:
            source,_=program(body,parameters=[])
            for mode in ('binary','v2'):
                d=self.directory('exact_'+name+'_'+mode)
                (d/'source.wgsl').write_bytes(source)
                before=cli_m5.tree(d)
                run,_=self.invoke(cli,self.parts(d,mode),d)
                errors=[]
                if admitted:
                    if run.returncode:
                        errors.append('Exact CLI capacity rejected: '+run.stderr)
                    else:
                        errors+=self.validate(d/'module.spv')
                elif run.returncode==0 or cli_m5.tree(d)!=before:
                    errors.append('CLI configured capacity not enforced atomically')
                self.record('exact_'+name+'_'+mode,errors,returncode=run.returncode,stderr=run.stderr)

    def faults(self, cli: Path, label: str) -> None:
        for mode in cli_m5.MODES:
            d=self.directory(label+'_trace_'+mode)
            (d/'source.wgsl').write_bytes(cli_m5.STORAGE)
            run,counts=self.invoke(cli,self.parts(d,mode),d)
            if run.returncode or counts is None:
                self.record(label+'_trace_'+mode,['Wrapped ordinary CLI failed'],stderr=run.stderr)
                continue
            errors=[]
            if counts['live'] or counts['live_descriptors']:
                errors.append('CLI leaked owned allocations/descriptors')
            if mode=='legacy':
                if counts['abi1_calls']!=1 or counts['abi2_calls'] or counts['queries'] or counts['job_queries'] or counts['job_calls']:
                    errors.append('Legacy mode did not exclusively use ABI1')
            else:
                if counts['job_queries']!=1 or counts['job_calls']!=1 or counts['abi1_calls'] or counts['abi2_calls'] or counts['queries']:
                    errors.append('Binary/v2 not exclusively queried job ABI')
                if counts['caps']!=list(DEFAULT_CAPACITIES.values()):
                    errors.append('CLI job capacities differ from frozen contract')
            expected_temps=1 if mode=='binary' else 2
            if counts['temps']!=expected_temps or counts['links']!=expected_temps or counts['lstats']!=expected_temps:
                errors.append('Optional sidecar temp/publication invariant failed')
            self.record(label+'_trace_'+mode,errors,trace=counts)
            binary=(d/'module.spv').read_bytes()
            sidecar=(d/'reflection.json').read_bytes() if mode!='binary' else None
            matrix=[(op,i) for op,key in [('allocation','allocations'),('open','opens'),('write','writes'),('close','closes'),('link','links'),('publish_collision','links'),('mkstemp','temps'),('unowned_temp','temps')]
                    for i in range(1,counts[key]+1)]
            matrix += [(op,1) for op in ['read','read_eintr','read_short']]
            matrix += [(op,i) for op in ['write_eintr','write_short'] for i in range(1,counts['writes']+1)]
            if mode!='legacy':
                matrix.append(('job_query',1))
            for operation,call in matrix:
                d=self.directory(label+'_fault_'+mode+'_'+operation+'_'+str(call))
                (d/'source.wgsl').write_bytes(cli_m5.STORAGE)
                before=cli_m5.tree(d)
                run,trace=self.invoke(cli,self.parts(d,mode),d,operation,call)
                errors=[]
                if trace is None or trace['faults']!=1:
                    errors.append('Fault injection did not execute exactly once')
                if trace and (trace['live'] or trace['live_descriptors']):
                    errors.append('Failure leaked owned allocation or descriptor')
                success=operation in {'read_eintr','read_short','write_eintr','write_short'}
                wanted=dict(before)
                if success:
                    if run.returncode:
                        errors.append('Retryable I/O did not complete')
                    for name,expected in [('module.spv',binary),('reflection.json',sidecar)]:
                        if expected is not None:
                            path=d/name
                            if not path.is_file() or path.read_bytes()!=expected:
                                errors.append('Retry changed or omitted '+name)
                            wanted[name]=['file',cli_m5.sha(expected)]
                else:
                    if not run.returncode:
                        errors.append('Injected failure succeeded')
                    if operation=='allocation' and not re.search(r'alloc|memory',run.stderr,re.IGNORECASE):
                        errors.append('Caller allocation failure lacks explicit allocation diagnostic')
                    if operation=='unowned_temp':
                        name='module.spv' if call==1 else 'reflection.json'
                        wanted[name+'.tmp.EVAL00']=['file',cli_m5.sha(b'UNOWNED')]
                    if operation=='publish_collision':
                        name='module.spv' if call==1 else 'reflection.json'
                        wanted[name]=['file',cli_m5.sha(b'UNOWNED')]
                if cli_m5.tree(d)!=wanted:
                    errors.append('Rollback/retry changed unowned file or left residue')
                self.record(label+'_fault_'+mode+'_'+operation+'_'+str(call),errors,trace=trace,returncode=run.returncode,stderr=run.stderr)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo',required=True,type=Path)
    parser.add_argument('--source-root',type=Path)
    parser.add_argument('--cli',required=True,type=Path)
    parser.add_argument('--library',type=Path)
    parser.add_argument('--build',type=Path)
    parser.add_argument('--cc',default='gcc')
    parser.add_argument('--cxx',default='g++')
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--validator',type=Path,default=VALIDATOR)
    parser.add_argument('--skip-sanitizer',action='store_true')
    args=parser.parse_args()
    owner=args.source_root or args.repo/'runtime/c/compiler'
    if not (owner/'src').is_dir():
        owner=owner/'runtime/c/compiler'
    output=args.out.resolve()
    output.mkdir(parents=True,exist_ok=True)
    stage=Stage(output/'build-receipts',args.build.resolve() if args.build else output/'build')
    checks=Checks(argparse.Namespace(repo=args.repo.resolve(),source_root=owner.resolve(),cli=args.cli.resolve(),validator=args.validator.resolve(),evidence=output,baseline=None))
    try:
        checks.positive(args.cli.resolve(),'candidate')
        checks.arguments(args.cli.resolve(),'candidate')
        checks.paths(args.cli.resolve(),'candidate')
        checks.capability_lifts(args.cli.resolve())
        sources=sorted((owner/'src').glob('*.c'))
        save(output/'wrapped-source-identities.json',{str(path):cli_m5.sha(path.read_bytes()) for path in [*sources,HERE/'job_cli_io_wrap_m7.c',Path(__file__).resolve()]})
        for label,extra in [('ordinary',[]),*([('sanitized',['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined','-fno-pie','-no-pie'])] if not args.skip_sanitizer else [])]:
            binary=stage.build/('cli-job-'+label)
            stage.run('build_'+label,[args.cc,*CFLAGS,*extra,'-I'+str(owner/'include'),*sources,HERE/'job_cli_io_wrap_m7.c','-Wl,'+','.join('--wrap='+name for name in WRAPS),'-o',binary])
            checks.faults(binary,label)
    except (OSError,RuntimeError,AssertionError,ValueError,subprocess.TimeoutExpired) as error:
        checks.record('stage_exception',[str(error)])
    return checks.finish()


if __name__=='__main__':
    raise SystemExit(main())
