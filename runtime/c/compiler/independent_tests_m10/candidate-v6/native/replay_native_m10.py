#!/usr/bin/env python3
"""Portable manifest-bound M10 native wrapper; sealed tests remain unmodified.
Post-seal orchestration only. Candidate bytes are source-root + fixed manifest,
not a moving Git worktree. No build unless --evaluate-candidate is supplied.
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, sys
MANIFEST='e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11'
SEAL='de784f1927dd9abde7272d60c3e3983fd0b65f6f8738c78f1ddc3923acfcd7dc'
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tests-root',required=True,type=Path,help='Unmodified sealed M10 native source directory')
    ap.add_argument('--source-root',type=Path,help='Verified runtime/c/compiler owner')
    ap.add_argument('--candidate-manifest',type=Path)
    ap.add_argument('--evaluate-candidate',action='store_true')
    ap.add_argument('--syntax-only',action='store_true')
    ap.add_argument('--out',type=Path)
    ap.add_argument('--build-root',type=Path)
    ap.add_argument('--skip-sanitizer',action='store_true')
    ap.add_argument('--cc',default='cc');ap.add_argument('--cxx',default='c++')
    args=ap.parse_args();tests=args.tests_root.resolve()
    if args.syntax_only and args.evaluate_candidate: ap.error('choose syntax-only or evaluation')
    if digest(tests/'SOURCE_SEAL.sha256')!=SEAL: raise RuntimeError('Wrong sealed evaluator')
    # Verify the original runner before loading any Python from the input tree.
    entries={}
    for line in (tests/'SOURCE_SEAL.sha256').read_text().splitlines():
        h,n=line.split('  ',1)
        if Path(n).is_absolute() or '..' in Path(n).parts: raise RuntimeError('Unsafe seal path')
        if digest(tests/n)!=h: raise RuntimeError('Modified sealed file '+n)
        entries[n]=h
    if len(entries)!=15: raise RuntimeError('Expected15 sealed inputs')
    spec=importlib.util.spec_from_file_location('sealed_native_m10',tests/'run_native_m10.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.HERE=tests;module.verify_seal()
    identities={};owner=None
    if args.source_root or args.candidate_manifest or args.evaluate_candidate:
        if not args.source_root or not args.candidate_manifest: ap.error('source-root and candidate-manifest are paired')
        owner=args.source_root.resolve()
        if digest(args.candidate_manifest)!=MANIFEST: raise RuntimeError('Wrong final-v6 manifest')
        for line in args.candidate_manifest.read_text().splitlines():
            h,n=line.split('  ',1)
            if not n.startswith('runtime/c/compiler/') or '..' in Path(n).parts: raise RuntimeError('Invalid source path')
            if digest(owner/n.removeprefix('runtime/c/compiler/'))!=h: raise RuntimeError('Changed candidate '+n)
            identities[n]=h
        if len(identities)!=30: raise RuntimeError('Expected30 manifest entries')
        bound={str((owner/n.removeprefix('runtime/c/compiler/')).resolve()) for n in identities}
        for group in ['src/**/*.c','src/**/*.h','include/**/*.h']:
            for p in owner.glob(group):
                if str(p.resolve()) not in bound: raise RuntimeError('Unmanifested input '+str(p))
        for p in (tests/'frozen_headers').glob('*.h'):
            if p.read_bytes()!=(owner/'include'/p.name).read_bytes(): raise RuntimeError('Frozen header differs '+p.name)
    def source_snapshot(repository,commit,output,receipt):
        if owner is None or repository.resolve()!=owner or commit!=MANIFEST: raise RuntimeError('Wrong source identity')
        module.write_json(receipt.output/'candidate-source-identities.json',dict(manifest_sha256=MANIFEST,files=identities))
        return owner,identities
    module.source_snapshot=source_snapshot
    original_run=module.Receipt.run
    def run(self,label,command,*pos,**kw):
        command=list(command)
        # Exactly the C harness compilations corrected in the executed v6 runner.
        # C++/syntax/candidate compilation and all test inputs stay unchanged.
        if label.startswith('build-') and tests/'allocator_m10.c' in command:
            command.insert(1,'-fno-builtin')
        return original_run(self,label,command,*pos,**kw)
    module.Receipt.run=run
    if not args.syntax_only and not args.evaluate_candidate:
        print(json.dumps(dict(status='PORTABLE_PLAN_VERIFIED',sealed_inputs=len(entries),candidate_entries=len(identities),candidate_compiled_or_executed=False,harness_only_flag='-fno-builtin',sanitizers_default=True),indent=2));return 0
    if not args.out: ap.error('--out required')
    if args.evaluate_candidate and not args.build_root: ap.error('--build-root required')
    argv=[str(tests/'run_native_m10.py'),'--out',str(args.out),'--cc',args.cc,'--cxx',args.cxx]
    if args.syntax_only: argv+=['--syntax-only']
    else:
        argv+=['--evaluate-candidate','--repository',str(owner),'--candidate',MANIFEST,'--build-dir',str(args.build_root)]
        if not args.skip_sanitizer: argv+=['--sanitize']
    sys.argv=argv;code=module.main()
    # The sealed runner's historical field is renamed in the supplemental receipt
    # only, because this source identity is a manifest, never a Git commit.
    report=args.out/'REPORT.json'
    if report.exists() and not args.syntax_only:
        d=json.loads(report.read_text());d['candidate_manifest_sha256']=d.pop('candidate_commit',MANIFEST)
        d['post_seal_orchestration']='Portable source manifest binding and harness-only -fno-builtin; original sealed tests untouched'
        module.write_json(args.out/'PORTABLE_REPORT.json',d)
    module.verify_seal()
    return code
if __name__=='__main__': raise SystemExit(main())
