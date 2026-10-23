#!/usr/bin/env python3
"""Independent manifest-bound M11 build, never reads a candidate by default."""
from pathlib import Path
import argparse,hashlib,json,os,subprocess,time
HERE=Path(__file__).resolve().parent
STRICT=['-std=c11','-O2','-fno-fast-math','-ffp-contract=off','-frounding-math','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror','-Wvla']
EXPORTS={'doe_compiler_compile','doe_compiler_workspace_requirements','doe_compiler_compile_v2','doe_compiler_job_requirements','doe_compiler_compile_job','doe_compiler_typed_requirements','doe_compiler_compile_typed'}
ALLOCATORS={'malloc','calloc','realloc','free','aligned_alloc','posix_memalign'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
class Receipt:
 def __init__(self,out):self.out=out;out.mkdir(parents=True,exist_ok=False);self.rows=[];self.failed=[]
 def run(self,label,command,required=True,timeout=600,env=None):
  start=time.monotonic();command=list(map(str,command));e=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');e.update(env or {})
  try:r=subprocess.run(command,capture_output=True,text=True,timeout=timeout,env=e);code,output=r.returncode,r.stdout+r.stderr
  except subprocess.TimeoutExpired as err:code,output=124,str(err)
  (self.out/(label+'.log')).write_text(output);row={'label':label,'command':command,'returncode':code,'seconds':time.monotonic()-start};self.rows.append(row);save(self.out/'commands.json',self.rows)
  if code:
   self.failed.append(row)
   if not (self.out/'FIRST_FAILURE.json').exists():save(self.out/'FIRST_FAILURE.json',row)
   if required:raise RuntimeError(label+' failed; original log preserved')
  print(('FAIL ' if code else 'PASS ')+label,flush=True);return output if code==0 else None

def verify(owner,manifest,frozen):
 entries={}
 for line in manifest.read_text().splitlines():
  h,n=line.split(None,1);n=n.lstrip('*');rel=n.removeprefix('runtime/c/compiler/')
  if Path(rel).is_absolute() or '..' in Path(rel).parts:raise RuntimeError('Unsafe source manifest path')
  p=owner/rel
  if sha(p)!=h:raise RuntimeError('Wrong source '+rel)
  entries[rel]=h
 for group in ['src/**/*.c','src/**/*.h','include/**/*.h']:
  for p in owner.glob(group):
   if str(p.relative_to(owner)) not in entries:raise RuntimeError('Unmanifested build input '+str(p))
 for p in frozen.glob('*.h'):
  if p.read_bytes()!=(owner/'include'/p.name).read_bytes():raise RuntimeError('Frozen header changed '+p.name)
 if len(list(frozen.glob('*.h')))!=4:raise RuntimeError('Expected four frozen headers')
 return entries

def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--evaluate-candidate',action='store_true');ap.add_argument('--owner',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--expected-manifest-sha256');ap.add_argument('--repo',type=Path,default=Path('/workspace/shared/doe-c-migration-20261023'));ap.add_argument('--build-root',type=Path);ap.add_argument('--out',type=Path);a=ap.parse_args()
 if not a.evaluate_candidate:print('PREPARATION ONLY: no candidate read or executed');return 0
 if not all([a.owner,a.manifest,a.expected_manifest_sha256,a.build_root,a.out]):ap.error('Explicit owner, manifest/hash, build-root and out are required')
 if sha(a.manifest)!=a.expected_manifest_sha256:raise RuntimeError('Wrong immutable manifest identity')
 owner=a.owner.resolve();frozen=a.repo/'runtime/c/compiler/independent_tests_m10/native/frozen_headers';entries=verify(owner,a.manifest,frozen);a.build_root.mkdir(parents=True,exist_ok=False);r=Receipt(a.out);save(r.out/'SOURCE_IDENTITY.json',{'manifest_sha256':sha(a.manifest),'owner':str(owner),'entries':entries,'headers':{p.name:sha(p) for p in frozen.glob('*.h')}})
 try:
  r.run('gcc-version',['gcc','--version']);r.run('gxx-version',['g++','--version']);objects=[]
  for source in sorted((owner/'src').glob('*.c')):
   if source.name.startswith('cli'):continue
   obj=a.build_root/(source.stem+'.o');r.run('object-'+source.stem,['gcc',*STRICT,'-fPIC','-fstack-usage','-fcallgraph-info=su','-I'+str(owner/'include'),'-I'+str(owner/'src'),'-c',source,'-o',obj]);objects.append(obj)
  archive=a.build_root/'libdoe-m11.a';shared=a.build_root/'libdoe-m11.so';cli=a.build_root/'doe-compiler-m11'
  r.run('archive',['ar','rcs',archive,*objects]);r.run('shared',['gcc','-shared',*objects,'-Wl,--version-script='+str(owner/'src/exports.map'),'-lm','-o',shared]);r.run('cli',['gcc',*STRICT,'-I'+str(owner/'include'),'-I'+str(owner/'src'),*sorted((owner/'src').glob('cli*.c')),*objects,'-lm','-o',cli])
  exports=r.run('exports',['nm','-D','--defined-only',shared]);actual={x.split()[-1].split('@')[0] for x in exports.splitlines() if len(x.split())>=3 and x.split()[-2].upper() in {'T','W'}}
  imports=r.run('imports',['nm','-u',archive]);allocators=ALLOCATORS&{x.split()[-1].split('@')[0] for x in imports.splitlines() if x.split()};r.run('shared-needed',['readelf','-d',shared])
  if actual!=EXPORTS or allocators:raise RuntimeError('Exports or allocation imports violate contract')
  if verify(owner,a.manifest,frozen)!=entries:raise RuntimeError('Source changed during build')
  result={'status':'PASS','manifest_sha256':sha(a.manifest),'owner':str(owner),'build_root':str(a.build_root),'artifacts':{p.name:{'path':str(p),'sha256':sha(p)} for p in [archive,shared,cli]},'exports':sorted(actual),'allocator_imports':sorted(allocators),'scope':'Fresh strict native source build only; no semantic/GPU/runtime acceptance'};save(r.out/'BUILD_RESULT.json',result);print(json.dumps(result,indent=2));return 0
 except Exception as e:save(r.out/'BUILD_RESULT.json',{'status':'FAIL','error':str(e),'failures':r.failed});raise
if __name__=='__main__':raise SystemExit(main())
