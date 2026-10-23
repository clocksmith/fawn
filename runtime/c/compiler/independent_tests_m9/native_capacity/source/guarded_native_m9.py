"""Postinspection storage-only orchestration, with memory-held compiler objects.

Preserves frozen fixtures/assertions. GCC/G++ source flags and source lists are
retained; -pipe changes only temporary transport. Sanitized compile/link stages
are split into ordinary compiler object jobs held in anonymous RAM files, then
linked to the original output. Every command/object hash is recorded. No source,
old evidence or accepted binary is modified. The final DSO and archive remain.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

HERE=Path(__file__).resolve().parent


def memory_build(stage, name: str, command: list, required: bool, extra_env: dict | None) -> str | None:
    command=list(map(str,command)); compiler=command[0]
    output=Path(command[command.index('-o')+1])
    source=[x for x in command[1:] if Path(x).suffix in ('.c','.cpp')]
    arguments=[x for i,x in enumerate(command[1:],1) if x not in source and x!='-o' and command[i-1]!='-o']
    flags=[x for x in arguments if x.startswith('-') and not x.startswith('-Wl,') and not x.startswith('-l') and x not in ('-shared','-no-pie')]
    if '-pipe' not in flags: flags.append('-pipe')
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');env.update(extra_env or {})
    fds=[]; transcript=[]; rows=[]; code=0; started=time.monotonic()
    try:
        for path in source:
            fd=os.memfd_create('m9-'+Path(path).stem,0);fds.append(fd)
            cmd=[compiler,*flags,'-c',path,'-o',f'/proc/self/fd/{fd}']
            run=subprocess.run(cmd,capture_output=True,text=True,env=env,pass_fds=(fd,),timeout=240)
            transcript.append(run.stdout+run.stderr); code=run.returncode
            os.lseek(fd,0,os.SEEK_SET); h=hashlib.sha256()
            for chunk in iter(lambda:os.read(fd,1024*1024),b''):h.update(chunk)
            rows.append(dict(command=cmd,returncode=code,anonymous_object_bytes=os.fstat(fd).st_size,anonymous_object_sha256=h.hexdigest()))
            if code:break
        if not code:
            cmd=[compiler,*arguments,*[f'/proc/self/fd/{fd}' for fd in fds],'-o',str(output)]
            run=subprocess.run(cmd,capture_output=True,text=True,env=env,pass_fds=tuple(fds),timeout=240)
            transcript.append(run.stdout+run.stderr);code=run.returncode
            rows.append(dict(command=cmd,returncode=code))
    except subprocess.TimeoutExpired as error:
        code=124;transcript.append(str(error))
    finally:
        for fd in fds:os.close(fd)
    text=''.join(transcript);(stage.output/(name+'.log')).write_text(text)
    (stage.output/(name+'-memory-build.json')).write_text(json.dumps(rows,indent=2,sort_keys=True)+'\n')
    row=dict(name=name,command=command,storage='anonymous-memory objects, -pipe',returncode=code,elapsed_seconds=time.monotonic()-started)
    stage.commands.append(row)
    (stage.output/'commands.json').write_text(json.dumps(stage.commands,indent=2,sort_keys=True)+'\n')
    if code:
        stage.failures.append(row)
        if not (stage.output/'FIRST_FAILURE.json').exists():(stage.output/'FIRST_FAILURE.json').write_text(json.dumps(row,indent=2)+'\n')
        print('FAIL',name,text[-1500:],flush=True)
        if required:raise RuntimeError(name+' failed')
        return None
    print('PASS',name,flush=True);return text


def worker() -> int:
    import compact_retained_m9 as compact
    import run_native_m9_reviewed as native
    import run_native_m9 as base
    original_native=native.Stage.run;original_base=base.Stage.run
    def wrapped(original):
        def run(self,name,command,required=True,extra_env=None):
            cmd=list(command)
            if str(cmd[0]) in ('gcc','g++'):
                if '-pipe' not in cmd:cmd.insert(1,'-pipe')
                if '-fsanitize=address,undefined' in cmd and '-c' not in cmd:
                    return memory_build(self,name,cmd,required,extra_env)
            return original(self,name,cmd,required,extra_env)
        return run
    native.Stage.run=wrapped(original_native);base.Stage.run=wrapped(original_base)
    return compact.main()


def usage_bytes(path: Path) -> int:
    total=0
    for file in path.rglob('*'):
        try:
            if file.is_file():total+=file.stat().st_size
        except FileNotFoundError:pass
    return total


def main() -> int:
    if '--worker' in sys.argv:
        sys.argv.remove('--worker');return worker()
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--soft-bytes',type=int,default=3932160)
    known,remaining=parser.parse_known_args()
    output=Path(remaining[remaining.index('--out')+1]).resolve()
    if output.exists():raise RuntimeError('fresh output required')
    temporary=output.parent/(output.name+'-compiler-temp');temporary.mkdir(exist_ok=False)
    env=dict(os.environ,TMPDIR=str(temporary),PYTHONDONTWRITEBYTECODE='1')
    command=[sys.executable,str(Path(__file__).resolve()),'--worker',*remaining]
    process=subprocess.Popen(command,env=env,start_new_session=True)
    record=dict(command=command,pid=process.pid,soft_bytes=known.soft_bytes,hard_budget_bytes=4194304,
                samples=0,peak_output_and_compiler_temp_bytes=0,
                initial_filesystem_free_bytes=shutil.disk_usage(output.parent).free,
                minimum_filesystem_free_bytes=shutil.disk_usage(output.parent).free,
                scope='20ms sampled newly generated stage output and compiler temp; unrelated workers excluded')
    while process.poll() is None:
        size=usage_bytes(output)+usage_bytes(temporary)
        record['samples']+=1
        record['peak_output_and_compiler_temp_bytes']=max(record['peak_output_and_compiler_temp_bytes'],size)
        record['minimum_filesystem_free_bytes']=min(record['minimum_filesystem_free_bytes'],shutil.disk_usage(output.parent).free)
        if size>known.soft_bytes:
            os.killpg(process.pid,signal.SIGSTOP)
            record.update(state='PAUSED_DISK_WATERMARK',observed_bytes=size)
            output.mkdir(parents=True,exist_ok=True)
            (output/'disk-guard.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
            print('PAUSED_DISK_WATERMARK',process.pid,size,flush=True)
            # Keep the process tree stopped and intact for explicit coordination.
            return 73
        time.sleep(0.02)
    record.update(state='FINISHED',returncode=process.returncode,final_bytes=usage_bytes(output)+usage_bytes(temporary))
    output.mkdir(parents=True,exist_ok=True)
    (output/'disk-guard.json').write_text(json.dumps(record,indent=2,sort_keys=True)+'\n')
    return process.returncode

if __name__=='__main__':raise SystemExit(main())
