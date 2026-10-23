#!/usr/bin/env python3
"""Source-only final main lane; native source-only lanes have their own runner."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,sys,time
HERE=Path(__file__).resolve().parent;ROOT=HERE.parent
p=argparse.ArgumentParser();p.add_argument('--owner',type=Path,required=True);p.add_argument('--source-manifest',type=Path,required=True);p.add_argument('--retained-root',type=Path,required=True);p.add_argument('--validator',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
for n,v in vars(a).items():setattr(a,n,v.resolve())
identity={n:h for h,n in (s.split(None,1) for s in a.source_manifest.read_text().splitlines())}
def verify():
 for n,h in identity.items():
  assert n.startswith('runtime/c/compiler/');assert hashlib.sha256((a.owner/n[len('runtime/c/compiler/'):]).read_bytes()).hexdigest()==h,n
verify();a.out.mkdir(parents=True,exist_ok=False);build=a.out/'build';build.mkdir();records=[]
old=a.retained_root/'runtime/c/compiler';m9=old/'independent_tests_m9/source';m8=old/'independent_tests_m8';m7=old/'independent_tests_m7'
env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH=os.pathsep.join(map(str,[m9,m8,m7])))
def run(name,args):
 command=list(map(str,args));start=time.monotonic();r=subprocess.run(command,capture_output=True,text=True,env=env);(a.out/(name+'.log')).write_text(r.stdout+r.stderr);records.append(dict(stage=name,returncode=r.returncode,seconds=time.monotonic()-start,command=command));(a.out/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
 if r.returncode:raise RuntimeError(name+' failed; original output retained')
 print('PASS',name,flush=True)
flags=['-std=c11','-O2','-fno-fast-math','-ffp-contract=off','-frounding-math','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror','-Wvla','-fPIC']
objects=[]
for source in sorted((a.owner/'src').glob('*.c')):
 if source.name.startswith('cli'):continue
 obj=build/(source.stem+'.o');run('compile-'+source.stem,['gcc',*flags,'-c',source,'-o',obj]);objects.append(obj)
lib=build/'libdoe_compiler.so';cli=build/'doe-compiler-c';archive=build/'libdoe.a'
run('shared',['gcc','-shared',*objects,'-Wl,--version-script='+str(a.owner/'src/exports.map'),'-lm','-o',lib]);run('archive',['ar','rcs',archive,*objects]);run('cli',['gcc',*flags,a.owner/'src/cli.c',a.owner/'src/cli_json.c',*objects,'-lm','-o',cli])
run('oracle-controls',[sys.executable,ROOT/'source/controls_m10.py','--validator',a.validator,'--receipt',a.out/'controls.json'])
run('main576',[sys.executable,ROOT/'candidate-v3/postdisclosure_semantic_adapter.py','--library',lib,'--validator',a.validator,'--receipt',a.out/'main576.json'])
run('lookup',[sys.executable,HERE/'lookup_acceptance.py','--library',lib,'--receipt',a.out/'lookup.json'])
run('boundaries',[sys.executable,ROOT/'source/boundaries_m10.py','--library',lib,'--target-width','--receipt',a.out/'boundaries.json'])
run('width-resource-limit',[sys.executable,HERE/'publish_width.py','--library',lib,'--validator',a.validator,'--receipt',a.out/'width.json'])
run('cli-faults',[sys.executable,ROOT/'source/cli_m10.py','--binary',cli,'--snapshot',a.owner,'--archive',archive,'--wrapped',build/'cli-wrapped','--receipt',a.out/'cli.json'])
for stage in ['historical','m8','m9']:
 run(stage,[sys.executable,HERE/'historical_lifts.py',stage,'--repo',a.retained_root,'--library',lib,'--validator',a.validator,'--out',a.out/stage,*(['--stage',stage] if stage!='m9' else ['--shared-apis'])])
run('same98',[sys.executable,HERE/'corpus_m10_final.py','--repo',a.retained_root,'--source-root',a.owner,'--cli',cli,'--validator',a.validator,'--out',a.out/'same98'])
run('retained-controls',[sys.executable,m9/'run_acceptance_m9_current.py','--repo',a.retained_root,'--source-root',a.owner,'--validator',a.validator,'--existing-build',build,'--evidence',a.out/'retained-controls','--stages','interval_oracle,unfinished_adapter_controls,instruction_oracle,retained_capacity,retained_postdisclosure'])
run('m9-mutants',[sys.executable,m9/'mutations_m9.py','--repo',a.retained_root,'--modules',a.out/'m9/modules','--validator',a.validator,'--out',a.out/'m9-mutants'])
run('m8-mutants',[sys.executable,m8/'mutations_m8.py','--repo',a.retained_root,'--scalar-modules',a.out/'m8/modules','--constant-modules',a.out/'m8/modules','--validator',a.validator,'--out',a.out/'m8-mutants'])
run('frames-records',[sys.executable,HERE/'stack_audit.py','--owner',a.owner,'--out',a.out/'stack'])
run('id-boundary-build',['gcc','-std=c11','-O2','-ffunction-sections','-fdata-sections','-I'+str(a.owner/'src'),HERE/'id_boundary.c','-Wl,--gc-sections','-o',build/'id-boundary']);run('id-boundary',[build/'id-boundary'])
verify();(a.out/'SUMMARY.json').write_text(json.dumps(dict(passed=True,source_manifest_sha256=hashlib.sha256(a.source_manifest.read_bytes()).hexdigest(),library_sha256=hashlib.sha256(lib.read_bytes()).hexdigest(),cli_sha256=hashlib.sha256(cli.read_bytes()).hexdigest(),stages=len(records),physical_device=False,native_lanes='separate runners'),indent=2)+'\n')
