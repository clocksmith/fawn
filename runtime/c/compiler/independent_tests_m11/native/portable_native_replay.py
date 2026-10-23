#!/usr/bin/env python3
"""Prepare an identity-bound M11 replay plan; execute only with --execute."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, subprocess, sys, time
OLD_REPO='/workspace/shared/doe-c-migration-20261023'
OLD_NATIVE='/workspace/shared/doe-c-migration-evidence-20261023/milestone11/evaluator/native'
OLD_BUILD='/tmp/doe-m11-independent-native-build2'
OLD_OWNER='/tmp/doe-m11-independent-native-candidate2/runtime/c/compiler'
OLD_AUTHOR='/tmp/doe-m11-author-build2/build'
INPUT_SHA='c5810c4b1fd2883469f749f9fc4a1077f8be1e2570f3d40bb99404dfdb496e17'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v): p.write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
def check(p,d):
 if sha(p)!=d: raise ValueError('Identity mismatch: '+str(p))
def safe_join(root,name):
 p=Path(name)
 if p.is_absolute() or '..' in p.parts: raise ValueError('Unsafe relative path: '+name)
 q=(root/p).resolve()
 if not q.is_relative_to(root): raise ValueError('Escaping input: '+str(q))
 return q

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['repo','owner','manifest','build-root','out']:p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--evidence',type=Path,default=Path(__file__).resolve().parent)
 p.add_argument('--cases',type=Path)
 p.add_argument('--prebuilt',type=Path);p.add_argument('--prebuilt-sha256')
 p.add_argument('--sanitize',action='store_true',help='Include the original ASan/UBSan recipes')
 p.add_argument('--retained',action='store_true',help='Also replay preserved M10/older consumer commands; no corpus copy')
 p.add_argument('--execute',action='store_true',help='Actually build/run; default only verifies inputs and writes plan')
 a=p.parse_args()
 for n in ['repo','owner','manifest','build_root','out','evidence']:
  setattr(a,n,getattr(a,n).resolve())
 a.cases=(a.cases or a.evidence.parent/'frozen').resolve()
 if a.prebuilt:a.prebuilt=a.prebuilt.resolve()
 roots=[a.repo,a.owner,a.evidence,a.cases,a.manifest.parent]
 for dest in [a.build_root,a.out]:
  if dest.exists():raise ValueError('Use a NEW destination: '+str(dest))
  if any(dest.is_relative_to(r) or r.is_relative_to(dest) for r in roots):raise ValueError('Destination overlaps immutable inputs: '+str(dest))
 if a.out.is_relative_to(a.build_root) or a.build_root.is_relative_to(a.out):raise ValueError('Output and build must be separate')
 for value in [*roots,a.build_root,a.out,*([a.prebuilt] if a.prebuilt else [])]:
  if any(c in str(value) for c in "'\"\\\n\r"):raise ValueError('Quoted/code-bearing path characters unsupported')
 inp=a.evidence/'PORTABLE_NATIVE_INPUTS.json';check(inp,INPUT_SHA);pins=json.loads(inp.read_text())
 check(a.manifest,pins['source_manifest_sha256'])
 seal=a.evidence/pins['original_native_manifest']['file'];check(seal,pins['original_native_manifest']['sha256'])
 native_count=0
 for line in seal.read_text().splitlines():
  d,n=line.split('  ',1);check(safe_join(a.evidence,n),d);native_count+=1
 for n,d in pins['repo_inputs'].items():check(safe_join(a.repo,n),d)
 for n,d in pins['frozen_work_inputs'].items():check(safe_join(a.cases,n),d)
 check(a.evidence/'boundary_m11_revision1.c',pins['corrected_boundary_sha256'])
 if a.prebuilt:
  if not a.prebuilt_sha256:raise ValueError('--prebuilt requires --prebuilt-sha256')
  check(a.prebuilt,a.prebuilt_sha256)
 elif a.prebuilt_sha256:raise ValueError('Hash requires --prebuilt')
 # Load the original, identity-checked build verifier, not a reimplementation.
 sys.dont_write_bytecode=True
 spec=importlib.util.spec_from_file_location('m11_original_build',a.evidence/'build_m11.py')
 mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
 frozen=a.repo/'runtime/c/compiler/independent_tests_m10/native/frozen_headers'
 entries=mod.verify(a.owner,a.manifest,frozen)
 a.out.mkdir(parents=True,exist_ok=False);relocated=a.out/'relocated';relocated.mkdir()
 mapping=[(OLD_NATIVE,str(a.evidence)),(str(Path(OLD_NATIVE).parent/'frozen'),str(a.cases)),(OLD_OWNER,str(a.owner)),(OLD_BUILD,str(a.build_root)),(OLD_REPO,str(a.repo))]
 if a.prebuilt:mapping.insert(0,(OLD_AUTHOR+'/libdoe_compiler.so',str(a.prebuilt)));mapping.insert(1,(OLD_AUTHOR,str(a.prebuilt.parent)))
 mapping.sort(key=lambda x:len(x[0]),reverse=True)
 def relocate(s):
  # Single pass prevents a replacement path from being relocated a second time.
  import re
  return re.sub('|'.join(re.escape(x) for x,_ in mapping),lambda m:dict(mapping)[m.group()],str(s))
 plan=[]
 def add(label,command,stdout=None,env=None):plan.append(dict(label=label,command=list(map(str,command)),stdout=str(stdout) if stdout else None,env=env or {}))
 def recipe(name,skip=lambda label:False):
  for row in json.loads((a.evidence/name).read_text()):
   label=row['label']
   if skip(label) or ('prebuilt' in label and not a.prebuilt) or (('san-' in label or 'sanitiz' in label) and not a.sanitize):continue
   cmd=[relocate(s) for s in row['command']]
   if cmd[0].endswith('python3') or cmd[0].endswith('python'):cmd[0]=sys.executable
   env={'PYTHONPATH':str(a.repo/'runtime/c/compiler/independent_tests_m7')} if label.startswith('retained-') else {}
   add(Path(name).parent.name+'-'+label,cmd,env=env)
 add('strict-build',[sys.executable,a.evidence/'build_m11.py','--evaluate-candidate','--repo',a.repo,'--owner',a.owner,'--manifest',a.manifest,'--expected-manifest-sha256',pins['source_manifest_sha256'],'--build-root',a.build_root,'--out',a.out/'build'])
 if a.retained:recipe('candidate2-consumers/commands.json',lambda label:'boundary_m11' in label)
 elif a.sanitize:
  recipe('candidate2-consumers/commands.json',lambda label:label!='sanitized-library')
 # Never replay the known-bad original boundary supplement. The original command
 # records remain intact; this is a separate routing decision, not source editing.
 recipe('candidate2-boundary-revision1/commands.json')
 recipe('candidate2-public-hardcap/commands.json')
 row=json.loads((a.evidence/'candidate2-boundary-revision1/commands.json').read_text())[0]['command']
 flags=row[1:next(i for i,s in enumerate(row) if s.startswith('-I'))]
 allocator=a.repo/'runtime/c/compiler/independent_tests_m10/native/allocator_m10.c'
 archive=a.build_root/'libdoe-m11.a'
 includes=['-I'+str(frozen),'-I'+str(a.owner/'src')]
 wraps=['-DWRAP_ALLOCATORS','-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=aligned_alloc,--wrap=posix_memalign','-Wl,--wrap=uniformity_analyze']
 for source,binary in [('private_work_m11.c','private-work'),('private_adapters_m11.c','private-adapters')]:
  add('build-'+binary,['gcc',*flags,*includes,a.evidence/source,allocator,archive,*wraps,'-pthread','-lm','-o',a.build_root/binary])
  args=[a.cases/n for n in sorted(pins['frozen_work_inputs'])] if binary=='private-work' else []
  add('run-'+binary,[a.build_root/binary,*args])
 if a.sanitize:recipe('candidate2-private-sanitizer/commands.json')
 add('build-layout',['gcc',*flags,*includes,a.evidence/'layout_m11.c',archive,'-lm','-o',a.build_root/'layout-m11'])
 add('run-layout',[a.build_root/'layout-m11'],stdout=relocated/'candidate2-layout.log')
 analyzer=a.repo/'runtime/c/compiler/independent_tests_m10/postdisclosure/analyze_stack.py'
 add('stack-parser',[sys.executable,analyzer,'--root',a.build_root])
 audit=a.evidence/'audit_layout_stack_m11.py';text=audit.read_text()
 # Only the two original absolute owner/build literals are changed in this copy.
 assert text.count(OLD_BUILD)==1 and text.count(OLD_OWNER)==1
 changed=text.replace(OLD_BUILD,str(a.build_root)).replace(OLD_OWNER,str(a.owner))
 out_audit=relocated/audit.name;out_audit.write_text(changed)
 add('ledger-stack-audit',[sys.executable,out_audit])
 id_source=a.repo/'runtime/c/compiler/independent_tests_m10/postdisclosure/id_boundary.c'
 add('build-id-boundary',['gcc',*flags,*includes,'-ffunction-sections','-fdata-sections',id_source,'-Wl,--gc-sections','-lm','-o',a.build_root/'id-boundary'])
 add('run-id-boundary',[a.build_root/'id-boundary'])
 save(a.out/'PLAN.json',plan)
 save(a.out/'IDENTITIES.json',dict(source_manifest_sha256=sha(a.manifest),source_entries=entries,native_manifest_sha256=sha(seal),original_native_files=native_count,repo_inputs=len(pins['repo_inputs']),frozen_work_inputs=len(pins['frozen_work_inputs']),input_metadata_sha256=sha(inp),wrapper_sha256=sha(Path(__file__)),relocations=mapping,ledger_script_original_sha256=sha(audit),ledger_script_relocated_sha256=sha(out_audit),corrected_boundary_sha256=pins['corrected_boundary_sha256'],prebuilt_sha256=sha(a.prebuilt) if a.prebuilt else None))
 if not a.execute:
  save(a.out/'RESULT.json',dict(status='PREPARED_NOT_EXECUTED',commands=len(plan),compiler_builds_or_tests_run=0,scope='Input identity and command-plan preparation only; no acceptance claim.'))
  print('PREPARED_NOT_EXECUTED '+str(a.out));return 0
 # Future execution is fail-fast, records original failures, never overwrites inputs.
 rows=[]
 for step in plan:
  cmd=step['command']
  if step['label']!='strict-build':
   if '-o' in cmd:Path(cmd[cmd.index('-o')+1]).parent.mkdir(parents=True,exist_ok=True)
   # The retained lifecycle helper owns its final legacy subdirectory.
  env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1');env.update(step['env'])
  start=time.monotonic()
  try:r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=600);code,output=r.returncode,r.stdout+r.stderr
  except subprocess.TimeoutExpired as e:code,output=124,str(e)
  (a.out/(step['label']+'.log')).write_text(output)
  if code==0 and step['stdout']:Path(step['stdout']).write_text(r.stdout)
  rows.append(dict(label=step['label'],command=cmd,returncode=code,seconds=time.monotonic()-start));save(a.out/'COMMANDS.json',rows)
  if code:
   save(a.out/'FIRST_FAILURE.json',rows[-1]);save(a.out/'RESULT.json',dict(status='FAIL',scope='Portable selected-gate replay',failure=rows[-1]));return 1
 if mod.verify(a.owner,a.manifest,frozen)!=entries:raise ValueError('Source changed during replay')
 save(a.out/'RESULT.json',dict(status='PASS_SELECTED_REPLAY',commands=len(rows),sanitize=a.sanitize,retained=a.retained,prebuilt=bool(a.prebuilt),scope='Only selected native controls; not target execution, full semantic acceptance or another-host qualification.'))
 return 0
if __name__=='__main__':raise SystemExit(main())
