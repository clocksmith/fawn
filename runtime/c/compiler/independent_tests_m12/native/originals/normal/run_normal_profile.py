#!/usr/bin/env python3
"""Source-bound normal Makefile CFLAGS stack remeasurement, no threshold edits."""
import argparse
import importlib.util
from pathlib import Path
import shlex
import sys

ORIGINAL = Path('/tmp/doe-m12-native-20261023/run_native_m12.py')
spec = importlib.util.spec_from_file_location('m12_native_original', ORIGINAL)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
p = argparse.ArgumentParser(description=__doc__)
for name in ('repo','cases','native','owner','manifest','build-root','out'):
    p.add_argument('--'+name, type=Path, required=True)
p.add_argument('--manifest-sha256', required=True)
a = p.parse_args()
for name in ('repo','cases','native','owner','manifest','build_root','out'):
    setattr(a, name, getattr(a, name).resolve())
for dest in (a.out,a.build_root):
    if dest.exists():
        raise ValueError('Fresh output required: '+str(dest))
    for src in (a.repo,a.cases,a.native,a.owner,ORIGINAL.parent):
        if dest.is_relative_to(src) or src.is_relative_to(dest):
            raise ValueError('Output/input overlap')
inputs,pins,identities = m.immutable_inputs(a)
m.check(a.manifest,a.manifest_sha256)
spec = importlib.util.spec_from_file_location('frozen_builder', a.native/'build_m11.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
frozen = a.repo/'runtime/c/compiler/independent_tests_m10/native/frozen_headers'
entries = builder.verify(a.owner,a.manifest,frozen)
makefile = a.owner/'Makefile'
lines = makefile.read_text().splitlines()
def field(name):
    found=[line.split('=',1)[1].strip() for line in lines if line.startswith(name+' =')]
    assert len(found)==1, name
    return found[0]
assert field('CC')=='gcc'
flags=shlex.split(field('CFLAGS'))
expected=builder.STRICT[:-1]
assert builder.STRICT[-1]=='-Wvla' and flags==expected, (flags,expected)
sources=[a.owner/name for name in shlex.split(field('LIB_SOURCES'))]
assert sorted(sources)==sorted(x for x in (a.owner/'src').glob('*.c') if not x.name.startswith('cli'))
assert len(sources)==16
assert entries['Makefile']==m.sha(makefile)
a.out.mkdir();a.build_root.mkdir()
r=m.Runner(a.out,a.manifest_sha256,'normal-makefile-stack')
identities.update(source_manifest_sha256=a.manifest_sha256,owner=str(a.owner),makefile_sha256=m.sha(makefile),compiler_flags=flags,measurement_flags=['-fstack-usage','-fcallgraph-info=su'],profile='Unchanged normal Makefile CFLAGS, non-PIC. Source objects do not use -fno-builtin or evaluator extra codegen flags.',original_runner_sha256=m.sha(ORIGINAL),normal_runner_sha256=m.sha(Path(__file__)))
m.save(a.out/'IDENTITIES.json',identities)
try:
    assert r.run('gcc-qualified-version',['gcc','-dumpfullversion']).strip()=='14.2.0'
    objects=[]
    for source in sources:
        obj=a.build_root/(source.stem+'.o')
        r.run('normal-object-'+source.stem,['gcc',*flags,'-fstack-usage','-fcallgraph-info=su','-c',source,'-o',obj])
        objects.append(obj)
    archive=a.build_root/'libdoe-m12-normal.a'
    r.run('normal-archive',['ar','rcs',archive,*objects])
    layout=a.build_root/'m12-normal-layout'
    r.run('build-layout-probe',['gcc',*m.C_FLAGS,'-I'+str(frozen),'-I'+str(a.owner/'src'),a.native/'layout_m11.c',archive,'-lm','-o',layout])
    r.run('run-layout-probe',[layout],a.out/'candidate2-layout.log')
    r.run('complete-stack-parser',[sys.executable,a.repo/'runtime/c/compiler/independent_tests_m10/postdisclosure/analyze_stack.py','--root',a.build_root])
    original=a.native/'audit_layout_stack_m11.py'
    text=original.read_text()
    changes={m.OLD_BUILD:str(a.build_root),m.OLD_OWNER:str(a.owner),'b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af':a.manifest_sha256}
    for old,new in changes.items():
        assert text.count(old)==1
        text=text.replace(old,new)
    relocated=a.out/'audit_layout_stack_m12_normal.py'
    relocated.write_text(text)
    m.save(a.out/'AUDIT_RELOCATION.json',dict(original=str(original),original_sha256=m.sha(original),relocated_sha256=m.sha(relocated),literal_changes=changes,expectations_changed=False))
    r.run('complete-normal-stack-layout-path-audit',[sys.executable,relocated])
    assert builder.verify(a.owner,a.manifest,frozen)==entries
    m.immutable_inputs(a)
    m.save(a.out/'RESULT.json',dict(status='PASS_NORMAL_MAKEFILE_STACK_PROFILE',source_manifest_sha256=a.manifest_sha256,commands=len(r.rows),build_root=str(a.build_root),stack_analysis=str(a.build_root/'stack-analysis.json'),stack_analysis_sha256=m.sha(a.build_root/'stack-analysis.json'),full_ledger=str(a.out/'candidate2-LEDGER_STACK.json'),archive_sha256=m.sha(archive),scope='Independent complete normal-flag library frame graph/layout/SCC/path audit. Does not substitute for strict-PIC native/sanitizer/target semantics. No compiler flags or stack limits changed.'))
except Exception as error:
    m.save(a.out/'RESULT.json',dict(status='INCOMPLETE_GATE' if isinstance(error,m.IncompleteGate) else 'FAIL_NORMAL_PROFILE',error=str(error),source_manifest_sha256=a.manifest_sha256))
    raise
