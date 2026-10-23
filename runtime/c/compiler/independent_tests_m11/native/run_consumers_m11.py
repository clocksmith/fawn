#!/usr/bin/env python3
"""Reuse one source-bound independent build for native affected-boundary checks."""
from pathlib import Path
import argparse,json,os,sys
from build_m11 import Receipt,sha,save,verify,STRICT
HERE=Path(__file__).resolve().parent
C=['-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-Wvla','-Wno-unused-const-variable','-Wno-misleading-indentation','-fno-fast-math','-ffp-contract=off','-frounding-math','-fno-builtin']
CPP=['-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-fno-exceptions','-fno-rtti']
ALLOC=['malloc','calloc','realloc','free','aligned_alloc','posix_memalign']
FENV=['fegetenv','feholdexcept','fesetenv','feupdateenv','fesetround','fegetround','feclearexcept','fesetexceptflag','feenableexcept','fedisableexcept']
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--evaluate-candidate',action='store_true');ap.add_argument('--build-receipt',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--repo',type=Path,default=Path('/workspace/shared/doe-c-migration-20261023'));ap.add_argument('--out',type=Path);ap.add_argument('--prebuilt',type=Path);ap.add_argument('--prebuilt-sha256');ap.add_argument('--sanitize',action='store_true');ap.add_argument('--selection',choices=['all','m10','m11','retained','sanitizer'],default='all');a=ap.parse_args()
 if not a.evaluate_candidate:print('PREPARATION ONLY: no candidate read or executed');return 0
 if not all([a.build_receipt,a.manifest,a.out]):ap.error('build-receipt, manifest and out required')
 b=json.loads(a.build_receipt.read_text());assert b['status']=='PASS' and sha(a.manifest)==b['manifest_sha256'];owner=Path(b['owner']);build=Path(b['build_root']);tests=a.repo/'runtime/c/compiler/independent_tests_m10/native';frozen=tests/'frozen_headers';entries=verify(owner,a.manifest,frozen)
 for art in b['artifacts'].values():assert sha(Path(art['path']))==art['sha256']
 for line in (tests/'SOURCE_SEAL.sha256').read_text().splitlines():h,n=line.split('  ',1);assert sha(tests/n)==h
 for line in (HERE/'SOURCE_SEAL.sha256').read_text().splitlines():h,n=line.split('  ',1);assert sha(HERE/n)==h
 r=Receipt(a.out);dest=build/('consumers-'+a.selection);dest.mkdir(exist_ok=False);inc='-I'+str(frozen);wrap='-Wl,'+','.join('--wrap='+n for n in ALLOC);archive=Path(b['artifacts']['libdoe-m11.a']['path']);shared=Path(b['artifacts']['libdoe-m11.so']['path']);links=[('archive',[archive]),('shared',[shared,'-Wl,-rpath,'+str(shared.parent)])]
 if a.prebuilt:
  assert a.prebuilt_sha256 and sha(a.prebuilt)==a.prebuilt_sha256
  save(r.out/'PREBUILT_IDENTITY.json',{'path':str(a.prebuilt),'sha256':sha(a.prebuilt),'scope':'Handed author prebuilt, never fresh independent source build'})
  links.append(('prebuilt',[a.prebuilt,'-Wl,-rpath,'+str(a.prebuilt.parent)]))
 def compile_run(label,source,link,extra=None,allocator=False,san=None,cpp=False):
  cmd=['g++' if cpp else 'gcc',*(CPP if cpp else C),*(san or []),inc,source]
  if allocator:cmd+=[tests/'allocator_m10.c']
  cmd+=list(link)+list(extra or [])+['-pthread','-lm','-o',dest/label]
  if r.run('build-'+label,cmd,required=False) is not None:r.run('run-'+label,[dest/label],required=False)
 for mode,link in links:
  extra=['-DWRAP_ALLOCATORS',wrap] if mode=='archive' else []
  if a.selection in ['all','m10']:
   for n in ['abi_m10.c','capacity_m10.c','host_m10.c','legacy_m10.c']:compile_run(mode+'-'+Path(n).stem,tests/n,link,extra,allocator=True)
   compile_run(mode+'-cpp-m10',tests/'consumer_m10.cpp',link,cpp=True)
  if a.selection in ['all','m11']:
   for n in ['boundary_m11.c','host_m11.c']:compile_run(mode+'-'+Path(n).stem,HERE/n,link,extra,allocator=True)
 if a.selection in ['all','m10']:compile_run('faults-m10',tests/'faults_m10.c',[archive],['-DWRAP_ALLOCATORS',wrap,'-Wl,'+','.join('--wrap='+n for n in FENV)],allocator=True)
 overlay=HERE/'retained_overlay';retained=a.repo/'runtime/c/compiler/independent_tests_m7';identity=json.loads((overlay/'OVERLAY_IDENTITY.json').read_text())
 for n,d in identity['identities'].items():assert sha(Path(d['original']))==d['original_sha256'] and sha(overlay/n)==d['overlay_sha256']
 if a.selection in ['all','retained']:
  compile_run('host-self-controls',overlay/'host_controls_m9.c',[])
  for mode,link in links:
   for n,define in [('host_environment_m9.c','WRAP_ALLOCATORS'),('public_exits_m9.c',None),('native_job_m9.c','JOB_WRAP_ALLOCATORS'),('native_m6_m9_boundary.c','WRAP_ALLOCATORS')]:compile_run(mode+'-'+Path(n).stem,overlay/n,link,['-D'+define,wrap] if mode=='archive' and define else [])
   for source in [overlay/'native_m9.cpp',retained/'native_job_m7.cpp',retained/'native_layout_m5.cpp']:compile_run(mode+'-'+source.stem,source,link,cpp=True)
  compile_run('faults-m9',overlay/'fenv_faults_m9.c',[archive],['-Wl,'+','.join('--wrap='+n for n in FENV)])
  code='import json; from pathlib import Path; from api_job_m7 import JobApi,query_checks,preflight_checks,lifecycle_checks; p=Path('+repr(str(shared))+'); a=JobApi(p); print(json.dumps({"query":query_checks(a),"preflight":preflight_checks(a),"lifecycle":lifecycle_checks(p)},indent=2)); a.close()'
  r.run('retained-query-preflight-lifecycle',[sys.executable,'-c',code],required=False,env={'PYTHONPATH':str(retained)})
  code='import json; from pathlib import Path; from native_job_m7 import legacy_boundaries; print(json.dumps(legacy_boundaries(Path('+repr(str(shared))+'),Path('+repr(str(dest/'legacy'))+')),indent=2))'
  r.run('retained-legacy-boundaries',[sys.executable,'-c',code],required=False,env={'PYTHONPATH':str(retained)})
 if a.sanitize or a.selection=='sanitizer':
  san=['-O1','-g','-fno-omit-frame-pointer','-fsanitize=address,undefined'];lib=dest/'libdoe-m11-sanitized.so';sources=[p for p in sorted((owner/'src').glob('*.c')) if not p.name.startswith('cli')]
  r.run('sanitized-library',['gcc',*STRICT,*san,'-fPIC','-shared','-I'+str(owner/'include'),'-I'+str(owner/'src'),*sources,'-Wl,--version-script='+str(owner/'src/exports.map'),'-lm','-o',lib]);link=[lib,'-Wl,-rpath,'+str(dest)];san+=['-fno-pie','-no-pie']
  for source in [*(tests/n for n in ['abi_m10.c','capacity_m10.c','host_m10.c','legacy_m10.c']),HERE/'boundary_m11.c',HERE/'host_m11.c']:compile_run('san-'+source.stem,source,link,allocator=True,san=san)
  for n in ['host_environment_m9.c','public_exits_m9.c','native_job_m9.c','native_m6_m9_boundary.c']:compile_run('san-'+Path(n).stem,overlay/n,link,san=san)
  save(r.out/'SANITIZER_IDENTITY.json',{'path':str(lib),'sha256':sha(lib)})
 assert verify(owner,a.manifest,frozen)==entries
 save(r.out/'RESULT.json',{'status':'PASS' if not r.failed else 'FAIL','source_manifest_sha256':sha(a.manifest),'selection':a.selection,'build_receipt_sha256':sha(a.build_receipt),'sanitizer':a.sanitize or a.selection=='sanitizer','failures':r.failed,'commands':len(r.rows),'scope':'Native host/ABI affected gates only; no GPU/runtime/full semantic acceptance'})
 return int(bool(r.failed))
if __name__=='__main__':raise SystemExit(main())
