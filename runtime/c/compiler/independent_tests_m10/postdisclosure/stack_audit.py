#!/usr/bin/env python3
"""Source-only GCC actual frame/SCC and compact-record reproduction."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('--owner',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.owner=a.owner.resolve();a.out.mkdir(parents=True,exist_ok=False)
flags=['-std=c11','-O2','-fno-fast-math','-ffp-contract=off','-frounding-math','-Wall','-Wextra','-Wpedantic','-Wconversion','-Wshadow','-Wstrict-prototypes','-Wmissing-prototypes','-Wswitch-enum','-Werror','-Wvla','-fPIC','-fstack-usage','-fcallgraph-info=su'];commands=[]
for source in sorted((a.owner/'src').glob('*.c')):
 if source.name.startswith('cli'):continue
 command=['gcc',*flags,'-c',str(source),'-o',str(a.out/(source.stem+'.o'))];commands.append(command);subprocess.run(command,check=True,capture_output=True,text=True)
subprocess.run([sys.executable,str(Path(__file__).with_name('analyze_stack.py')),'--root',str(a.out)],check=True,capture_output=True,text=True)
expected={'CompilerWorkspace':(512,8),'Expression':(40,4),'Statement':(56,4),'ConstantSyntax':(40,8),'FunctionRecord':(80,4),'FormalRecord':(16,4),'ArgumentSyntax':(8,4),'GenericSyntax':(24,4),'TypeRecord':(56,8),'MemberRecord':(56,8),'ResourceRecord':(104,8),'ConstantRecord':(32,8),'ConstantValue':(16,8),'AccessStep':(24,4),'IrInstruction':(32,4),'IrLocal':(8,4),'LocalBinding':(48,8),'TargetDeclaration':(32,8),'WalkItem':(16,4),'Token':(12,4),'LegacyWorkspace':(140056,8)}
source='#include "internal.h"\n#include <stdio.h>\n#define ROW(T) printf("%s %zu %zu\\n",#T,sizeof(T),_Alignof(T))\nint main(void){'+''.join('ROW('+n+');' for n in expected)+'return 0;}\n'
(a.out/'layout.c').write_text(source);command=['gcc','-std=c11','-O2','-Wall','-Wextra','-Wpedantic','-Werror','-I'+str(a.owner/'src'),str(a.out/'layout.c'),'-o',str(a.out/'layout')];commands.append(command);subprocess.run(command,check=True,capture_output=True,text=True)
output=subprocess.check_output([str(a.out/'layout')],text=True);actual={n:(int(s),int(al)) for n,s,al in map(str.split,output.splitlines())};assert actual==expected,(actual,expected)
r=json.loads((a.out/'stack-analysis.json').read_text());assert r['wrapper']<=147968 and r['sum_all_other_frames']<=16384 and r['maximum_nonwrapper'][0]<=8192
for scc in r['sccs']:
 names=' '.join(scc['members']);limit=512 if ':expression' in names and ':primary' in names else 256 if ':statement' in names else 768 if ':lower_block' in names else 384 if ':emit_block' in names else 1536
 assert scc['bytes']<=limit,(scc,limit)
assert r['fixed_combined']<=212992 and r['job_combined']<=196608
report=dict(passed=True,commands=commands,records=actual,frames=r,source_sha256={str(p.relative_to(a.owner)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((a.owner/'src').glob('*')) if p.is_file()},scope='GCC14 Linux LP64 C-owned frames; external C runtime internals and physical GPU excluded')
(a.out/'AUDIT.json').write_text(json.dumps(report,sort_keys=True,indent=2)+'\n');print(json.dumps({k:r[k] for k in ['wrapper','sum_all_other_frames','fixed_combined','job_combined']}))
