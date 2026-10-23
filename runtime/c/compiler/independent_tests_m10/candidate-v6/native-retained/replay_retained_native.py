#!/usr/bin/env python3
"""Replay unchanged pinned M9 native tests, then exact frozen-M10 overlays.
Post-inspection orchestration, not a new preinspection test seal. No candidate edits.
Python 3.10+, GCC/G++, ar, nm and readelf; Linux native host; no downloads.
"""
from pathlib import Path
import argparse, ast, datetime, hashlib, json, os, re, shutil, subprocess, sys
HERE = Path(__file__).resolve().parent
MANIFEST_SHA = 'e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11'

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p, obj): p.write_text(json.dumps(obj, indent=2, sort_keys=True)+'\n')
def check(condition, message):
    if not condition: raise RuntimeError(message)
def exact_replace(text, old, new, count=1):
    check(text.count(old)==count, 'Unexpected replacement count: '+old)
    return text.replace(old, new)
def fixture(text, name):
    match=re.search(r'static const char '+name+r'\[\]\s*=\s*((?:"(?:[^"\\]|\\.)*"\s*)+);',text)
    check(match is not None,'Missing fixture '+name)
    return ''.join(ast.literal_eval(v) for v in re.findall(r'"(?:[^"\\]|\\.)*"',match.group(1)))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source-root', required=True, type=Path, help='Manifest-bound runtime/c/compiler owner')
    ap.add_argument('--retained-root', required=True, type=Path, help='Pinned dee5 repository root')
    ap.add_argument('--candidate-manifest', required=True, type=Path)
    ap.add_argument('--out', required=True, type=Path, help='New compact receipt directory')
    ap.add_argument('--build-root', required=True, type=Path, help='New temporary build directory')
    ap.add_argument('--record-audit', type=Path, help='Optional separately owned M10 record/stack receipt, hash-bound, not rerun here')
    ap.add_argument('--skip-sanitizer', action='store_true')
    args=ap.parse_args()
    owner,repo,out,build=(x.resolve() for x in (args.source_root,args.retained_root,args.out,args.build_root))
    inputs=json.loads((HERE/'RETAINED_INPUTS.json').read_text())
    check(sha(args.candidate_manifest)==MANIFEST_SHA,'Wrong final-v6 source manifest')
    candidate={}
    for line in args.candidate_manifest.read_text().splitlines():
        h,name=line.split(None,1); name=name.lstrip('*')
        check(name.startswith('runtime/c/compiler/') and '..' not in Path(name).parts,'Invalid candidate path')
        candidate[name.removeprefix('runtime/c/compiler/')]=h
    check(len(candidate)==30,'Expected exactly30 source entries')
    def verify():
        for name,h in inputs['files'].items(): check(sha(repo/name)==h,'Changed pinned input '+name)
        for name,h in candidate.items(): check(sha(owner/name)==h,'Changed candidate input '+name)
        allowed={str((owner/name).resolve()) for name in candidate}
        for group in ['src/*.c','src/*.h','include/*.h']:
            for p in owner.glob(group): check(str(p.resolve()) in allowed,'Unmanifested build input '+str(p))
    verify()
    check(not out.exists() and not build.exists(),'Use fresh out and build-root; earlier receipts are immutable')
    out.mkdir(parents=True); build.mkdir(parents=True)
    old=repo/'runtime/c/compiler/independent_tests_m9/native_capacity/source'
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1')
    common=['--source-root',str(owner),'--retained-root',str(repo)]
    if args.skip_sanitizer: common.append('--skip-sanitizer')
    # Preserve the original runner's real failures before applying any overlay.
    original=build/'original'
    cmd=[sys.executable,str(old/'run_native_m9_final.py'),*common,'--out',str(original)]
    with (out/'original_stdout.log').open('w') as log:
        p=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,env=env,check=False)
    original_rc=p.returncode
    original_receipts=out/'original'; original_receipts.mkdir()
    for p in original.iterdir():
        if p.is_file() and p.suffix in {'.json','.log','.txt'}: shutil.copyfile(p,original_receipts/p.name)
    original_report=json.loads((original_receipts/'native-report.json').read_text())
    check(original_report['status']=='FAIL' and (original_receipts/'FIRST_FAILURE.json').exists(),'Original superseded assertions must remain visible')
    overlay=out/'sources'; overlay.mkdir(); changes=[]; originals={}
    def copy(name,origin=None):
        p=origin or old/name; originals[str(p.relative_to(repo))]=sha(p); (overlay/name).write_bytes(p.read_bytes())
    def replace(name,old_text,new_text,reason):
        p=overlay/name; p.write_text(exact_replace(p.read_text(),old_text,new_text));changes.append(dict(file=name,old=old_text,new=new_text,occurrences=1,reason=reason))
    for name in ['host_support_m9.h','fixtures_m9.h','host_controls_m9.c','host_environment_m9.c','public_exits_m9.c','native_m9.cpp','native_job_m9.c','fenv_faults_m9.c','native_m6_m9_boundary.c']: copy(name)
    copy('native_m5.c',repo/'runtime/c/compiler/independent_tests_m7/native_m5.c')
    for name in ['host_support_m9.h','native_job_m9.c','native_m6_m9_boundary.c']:
        replace(name,'122880u','147456u','Frozen M10 section6/storage ledger fixed-workspace ceiling only; no other pool or boundary raised.')
    src='@compute @workgroup_size(1) fn main(@builtin(global_invocation_id) a:vec3u){let x=a;}'
    m6=(overlay/'native_m6_m9_boundary.c').read_text()
    arr=re.search(r'static const char \*const m6_invalid\[\] = \{(.*?)\n\};',m6,re.S).group(1)
    rows=re.findall(r'"(?:[^"\\]|\\.)*"',arr)
    check(ast.literal_eval(rows[9])==src and hashlib.sha256(src.encode()).hexdigest()=='ba88f71337626997c0a81f48f7e9eb540ca36b04456195cc0594defeb078f26c','Unexpected vector snapshot fixture')
    replace('native_m6_m9_boundary.c','m6_atomic_call(workspace, m6_invalid[i], strlen(m6_invalid[i]), WORDS, 0);','m6_atomic_call(workspace, m6_invalid[i], strlen(m6_invalid[i]), WORDS, i == 9u ? 1 : 0);','Exact SHA ba88f713... row9 only, M10 section2 whole-vector local snapshot and section8 legacy grammar. All other negative/mutation checks retained.')
    replace('native_m6_m9_boundary.c','7 positive/13 negative seeds','7 original positive/12 original negative/1 exact M10 positive-lift seeds','Informational count only; no omitted fixture.')
    expected='8cf616ceed349c909209b37a777a8b66bd58c8117e260958b1dddba541d678b6'
    check(hashlib.sha256(fixture((overlay/'native_m5.c').read_text(),'unsupported').encode()).hexdigest()==expected,'Unexpected M5 uniform fixture')
    replace('native_m5.c','failure(workspace, unsupported, sizeof(unsupported)-1u, WORDS, DOE_COMPILER_UNSUPPORTED);','failure(workspace, unsupported, sizeof(unsupported)-1u, WORDS, DOE_COMPILER_SEMANTIC_ERROR);','Exact fixture SHA '+expected+': M10 section4 admits array type but rejects conservative-uniform stride4; still negative with all atomicity checks.')
    expected='890fcacb0d44a98b7e845ca7627d164cb0c89162a2c7d7da4493e6bb5433369e'
    check(hashlib.sha256(fixture((overlay/'public_exits_m9.c').read_text(),'unsupported_source').encode()).hexdigest()==expected,'Unexpected M9 uniform fixture')
    replace('public_exits_m9.c','case 8u: source=unsupported_source; length=strlen(source); expected=DOE_COMPILER_UNSUPPORTED; break;','case 8u: source=unsupported_source; length=strlen(source); expected=DOE_COMPILER_SEMANTIC_ERROR; break;','Distinct exact fixture SHA '+expected+': same frozen M10 section4 invalid uniform stride4; status3 to4 only, every public-exit FP/errno check unchanged.')
    s=(old/'run_native_m9_final.py').read_text()
    originals[str((old/'run_native_m9_final.py').relative_to(repo))]=sha(old/'run_native_m9_final.py')
    s=exact_replace(s,'HERE = Path(__file__).resolve().parent','HERE = Path('+repr(str(old))+')\nTEST_ROOT = Path('+repr(str(overlay))+')')
    s=exact_replace(s,"self.build = output/'build'","self.output.mkdir(parents=True, exist_ok=False)\n        self.build = Path("+repr(str(build/'adapted'/'build'))+")")
    s=exact_replace(s,"'doe_compiler_compile_job'}","'doe_compiler_compile_job', 'doe_compiler_typed_requirements', 'doe_compiler_compile_typed'}")
    s=exact_replace(s,"limit=123136 if function=='doe_compiler_compile' else 8192","limit=147968 if function=='doe_compiler_compile' else 8192")
    for name in ['host_controls_m9.c','host_environment_m9.c','public_exits_m9.c','native_m9.cpp','native_job_m9.c','fenv_faults_m9.c','native_m6_m9_boundary.c']:
        needle="HERE/'"+name+"'";check(needle in s,'Missing runner input '+name);s=s.replace(needle,"TEST_ROOT/'"+name+"'")
    start=s.index("        stage.run('private_layout_build'");end=s.index("        retained = repo/",start)
    s=s[:start]+"        report['private_layout_disposition'] = 'Original obsolete M9 private-layout compile failure retained separately; not credited as pass. Frozen M10 records/combined-stack require separate parent-owned audit.'\n"+s[end:]
    s=s.replace("['gcc', *CFLAGS, include, TEST_ROOT/","['gcc', *CFLAGS, '-fno-builtin', include, TEST_ROOT/")
    s=exact_replace(s,'[compiler, *flags, include, source,',"[compiler, *flags, '-fno-builtin', include, source,")
    s=exact_replace(s,"['gcc', *CFLAGS, *flags, '-fno-pie'","['gcc', *CFLAGS, *flags, '-fno-builtin', '-fno-pie'")
    s=exact_replace(s,"claim='Linux x86-64 GCC14.2 native compiler host only, not GPU qualification'","claim='Pinned retained M9 native behavior with exact frozen-M10 adapters; no obsolete private-layout, combined-stack or GPU qualification'")
    generated=out/'generated_runner.py';generated.write_text(s)
    save(out/'ADAPTERS.json',dict(provenance='Post-candidate-inspection exact frozen-contract reconciliation; not a preinspection seal',original_inputs=originals,source_adapters=changes,adapted_sources={p.name:sha(p) for p in overlay.iterdir()},generated_runner_sha256=sha(generated),orchestration_adapters=['Keep original seals and historical originals unchanged; only paths redirect to overlay copies','Exactly7 exports: original5 plus additive2 ABI4 exports','ABI1 frame123136 to frozen147968; every other frame8192 unchanged','Private-layout original compile failure preserved, not credited as pass; separate frozenM10 audit required','Harness-only -fno-builtin correction preserves allocator positive-control stores; candidate flags unchanged','Move bulky builds into requested fresh temporary tree']))
    cmd2=[sys.executable,str(generated),*common,'--out',str(out/'adapted')]
    with (out/'adapted_stdout.log').open('w') as log:
        p=subprocess.run(cmd2,stdout=log,stderr=subprocess.STDOUT,env=env,check=False)
    report=json.loads((out/'adapted'/'native-report.json').read_text());verify()
    result=dict(status='PASS_RETAINED_NATIVE_WITH_ENUMERATED_M10_ADAPTERS' if p.returncode==0 and report['status']=='PASS' else 'FAIL',utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),original_returncode=original_rc,original_command=cmd,adapted_returncode=p.returncode,adapted_command=cmd2,original_report_sha256=sha(original_receipts/'native-report.json'),original_first_failure_sha256=sha(original_receipts/'FIRST_FAILURE.json'),adapted_report_sha256=sha(out/'adapted'/'native-report.json'),candidate_manifest_sha256=MANIFEST_SHA,pinned_input_manifest_sha256=sha(HERE/'RETAINED_INPUTS.json'),original_inputs_unchanged=len(inputs['files']),candidate_sources_unchanged=len(candidate),candidate_source_edits=False,pinned_test_edits=False,sanitizers_run=not args.skip_sanitizer,scope='Native host behavior only. Original obsolete private layout is not passed; full semantic/corpus/combined-stack/GPU acceptance excluded.')
    if args.record_audit:
        audit=json.loads(args.record_audit.read_text());check(audit['passed'] is True,'Separate M10 audit did not pass')
        for name,h in audit['source_sha256'].items(): check(candidate.get(name)==h,'Separate audit source identity mismatch '+name)
        shutil.copyfile(args.record_audit,out/'SEPARATE_M10_RECORD_AUDIT.json')
        result['separate_parent_record_audit']=dict(sha256=sha(args.record_audit),records=len(audit['records']),scope=audit['scope'],disposition='Separate independent M10 measurement replaces, but does not pass, obsolete M9 private layout')
    save(out/'RESULT.json',result)
    entries=[]
    for p in sorted(out.rglob('*')):
        if p.is_file(): entries.append(sha(p)+'  '+str(p.relative_to(out)))
    (out/'EVIDENCE.sha256').write_text('\n'.join(entries)+'\n')
    print(json.dumps(result,indent=2));return int(result['status']=='FAIL')
if __name__=='__main__': raise SystemExit(main())
