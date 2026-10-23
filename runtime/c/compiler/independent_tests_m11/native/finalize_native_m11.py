#!/usr/bin/env python3
"""Finalize independent native evidence; does not execute or modify the compiler."""
from pathlib import Path
import datetime,hashlib,json,subprocess
H=Path(__file__).resolve().parent; B=Path('/tmp/doe-m11-independent-native-build2'); O=Path('/tmp/doe-m11-independent-native-candidate2/runtime/c/compiler')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads((H/p).read_text())
def save(p,v):(H/p).write_text(json.dumps(v,indent=2,sort_keys=True)+'\n')
sid=read('candidate2-build/SOURCE_IDENTITY.json')
assert all(sha(O/p)==digest for p,digest in sid['entries'].items())
save('FINAL_SOURCE_RECHECK.json',dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),entries=len(sid['entries']),manifest_sha256=sid['manifest_sha256'],unchanged=True))
# No allocation-bearing imports were found in the original independent build.
# Audit data symbols: two relocation-bearing constant tables are not mutable state.
data=[]
for obj in sorted(B.glob('*.o')):
 for line in subprocess.check_output(['nm','-S','--defined-only',str(obj)],text=True).splitlines():
  v=line.split()
  if len(v)!=4 or v[2].lower() not in ['b','d','g','s','c']:continue
  table=subprocess.check_output(['objdump','-t',str(obj)],text=True)
  rows=[x for x in table.splitlines() if x.split() and x.split()[-1]==v[3]]
  assert len(rows)==1 and '.data.rel.ro.local' in rows[0],(obj,v,rows)
  data.append(dict(object=obj.name,name=v[3],size=int(v[1],16),section='.data.rel.ro.local',objdump=rows[0]))
assert {r['name'] for r in data}=={'scalar_builtins','forbidden_names'}
assert 'static const ScalarBuiltin scalar_builtins[]' in (O/'src/numeric.c').read_text()
assert 'static const char *const forbidden_names[]' in (O/'src/sema_declarations.c').read_text()
save('candidate2-GLOBAL_STATE.json',dict(status='PASS',data_symbols=data,source_const_qualified=True,mutable_data_or_bss_symbols=[],allocator_imports=read('candidate2-build/BUILD_RESULT.json')['allocator_imports'],scope='Compiler-owned object symbols and source declarations; caller and C runtime state excluded. Reentrancy/concurrency supported separately by consumer tests.'))
# Frozen conservative bound is receipt-only. Use checked saturating-u64 arithmetic,
# including saturation in power, and module capacities rather than source estimates.
U=(1<<64)-1
def add(a,b):return U if a>U-b else a+b
def mul(a,b):return U if b and a>U//b else a*b
def power(a,n):
 z=1
 for _ in range(n):z=mul(z,a)
 return z
limits={}
for line in (H/'candidate2-layout.log').read_text().splitlines():
 v=line.split()
 if v[0]=='LIMITS':limits[v[1]]=dict(zip('T E S I Q M R X K L O D V W PE TS CE CC'.split(),map(int,v[2:])))
bounds={}
for name,l in limits.items():
 P=l['T']//4;F=l['T']//6;G=l['T']//8;Ls=l['S']+1
 factors=[l['PE']+l['E']+2*l['X']+P+F+l['S']+32,6*Ls+32,power(Ls+2,l['CC']),P+2*F,l['S']+P+F+l['R']+G+l['Q']+1]
 z=1
 for v in factors:z=mul(z,v)
 bounds[name]=dict(Ls=Ls,maximum_loop_nesting_bound=l['CC'],factors=factors,bound=z,saturated=z==U)
save('candidate2-LOOSE_WORK_BOUND.json',dict(status='PASS',arithmetic='Checked saturating u64 add/multiply/power',formula='(physicalE+E+2X+P+F+S+32)*(6Ls+32)*(Ls+2)^control_depth*(P+2F)*(S+P+F+R+G+Q+1)',interpretation='Capacity-only conservative module bound; each function has <=control_depth active loops and <=Ls locals, sum(P_f+2)<=P+2F. This estimate is not an admission limit.',profiles=bounds,non_admission_evidence='All 17 private-work positives pass using the saturated typed_cli profile, and the public hardcap no-barrier twin succeeds at the same saturated capacity shape. Only actual counted dependency work governs exhaustion.'))
first=read('candidate2-consumers/RESULT.json');revision=read('candidate2-boundary-revision1/RESULT.json')
assert first['commands']==113 and len(first['failures'])==4
assert all('boundary_m11' in row['label'] for row in first['failures'])
assert revision['status']=='PASS'
for p in ['candidate2-build/BUILD_RESULT.json','candidate2-private-sanitizer/RESULT.json','candidate2-public-hardcap/RESULT.json','candidate2-LEDGER_STACK.json','candidate2-TARGET_COUNTS.json']:
 assert read(p)['status']=='PASS',p
stack=read('candidate2-LEDGER_STACK.json'); build=read('candidate2-build/BUILD_RESULT.json')
counts={}
for line in (H/'candidate2-private-work.log').read_text().splitlines():
 v=line.split();counts[Path(v[0]).name]=dict(x.split('=',1) for x in v[1:])
assert len(counts)==17
receipt=dict(status='PASS_WITH_PRESERVED_EVALUATOR_CORRECTIONS',native_gates_unqualified=[],candidate_defects_observed=[],source_manifest_sha256=sid['manifest_sha256'],source_entries_unchanged=30,accepted_M10_commit='2220c64cff0bd87b1eed6b246515941ce7d18d24',independent_build=build['artifacts'],author_prebuilt=dict(path='/tmp/doe-m11-author-build2/build/libdoe_compiler.so',sha256=sha(Path('/tmp/doe-m11-author-build2/build/libdoe_compiler.so')),provenance='Handed author artifact, not classified as the independent source build'),
 native_routes=['fresh strict GCC14 source-built archive','fresh source-built shared library','handed author shared prebuilt','ASan+UBSan affected consumers'],
 public_compatibility=dict(headers_unchanged=4,exports=build['exports'],query_sizes={k:v['total'] for k,v in stack['profiles'].items()},alignment='Public ABI3/ABI4 alignment16 is sufficient and divisible by private record alignment8; public ABI2 remains8. Equality is neither required nor assumed.',old_span_behavior='Stale M10 exact byte spans reject before use; all three current exact spans and one-byte-short controls checked.'),
 first_native_receipt=dict(path='candidate2-consumers/RESULT.json',commands=113,successful_commands=109,preserved_failure_count=4,failure_scope='All four failures came from the original evaluator boundary supplement, not candidate defects.'),
 corrections=[dict(receipt='candidate2-boundary-revision1/CORRECTION.json',description='Only two public alignment assertions and two REFLECTION-only output descriptor selections corrected. Original supplement source, seal, first report and all failure logs remain preserved. Affected four routes rebuilt/rerun only.',accepted_control='ACCEPTED_M10_ALIGNMENT_EVIDENCE.json'),dict(receipt='STACK_PARSER_CORRECTION.json',description='GCC .su clone-name numeric suffix normalization only; original parser preserved. Full unit/size/count and graph checks retained. Localalias edge separately resolved by matching nm addresses.')],
 host_and_lifetime=dict(status='PASS',checks=['All selected ABI4 output modes, success/failure atomicity and reuse','Exact and one-short spans, invalid alignment, redzones and retained public preflight/lifecycle gates','errno and full supported fenv host profiles including FTZ/DAZ and traps, plus injected numeric-environment failures','Same-process reuse and concurrent independent caller workspaces','Allocator interposition self-controls and zero observed allocation in protected compilation windows; no allocator imports','ASan+UBSan affected consumers, plus all17 private-work sources in all6 modes','Full secondary scratch poisoning before uniformity and protected persistent IR/type/resource/access/operand hashes afterward'],phase_order='Tokens die after parse; typed resolver Walk finishes with sema; completed sema and cycle checks precede uniformity B2; complete uniformity precedes B1 target/reflection emission. Persistent typed structures are not overlaid.',alias_scope='Full-capacity source/workspace/output/result nonoverlap remains the public caller precondition. No arbitrary-overlap rejection guarantee was invented or tested.'),
 work=dict(ceiling=67108864,scope='Frozen dependency-operation budget, not a cap on every compiler memory read',pre_scan_boundary='uniformity.c:410-415 scans at most expression_count callee-marker positions before creating the budget. This marker-classification pass is uncharged. Parser expression allocation bounds expression_count by physical_expressions; the allocated callee map has that capacity. The pass detects absence of resolved barriers and performs no sensitivity run. It is not counted as an AST dependency visit or call-graph edge traversal. All recorded N values exclude it.',charged_analysis='Budget instantiated at uniformity.c:416. Subsequent retained AST visits, map/reset/copy/compare/join operations, formal/summary/operand/access inspections, identity comparisons and call-graph edges use checked charges before work. The no-barrier witness reports N=0.',source_bound_private=dict(source='private_work_m11.c',method='Link-time wrap of unchanged uniformity_analyze; no public export and no production edit',cases=counts,modes_per_case=6,controls='Exact N passes, N-1 UNSUPPORTED with exact diagnostic and no partial publication, recovery after failure; no-barrier zero-work pass',all_adapters_receipt='candidate2-private-adapters.log',all_adapters_N={'ABI1':2325,'ABI2':2325,'ABI3':23957,'ABI4':23957}),public_default_cap=dict(receipt='candidate2-public-hardcap/RESULT.json',fixture='1000 unused scalar formals plus24 sequential loops, barrier/no-barrier twin',success='ABI3 and all6 ABI4 modes exhaust the unchanged public ceiling with UNSUPPORTED, exact work-limit diagnostic and atomic output; no-barrier twin succeeds in the same pools',fixed_adapters='This large-formal fixture hits the unchanged fixed source/token pool first on ABI1/ABI2, producing INPUT_ERROR, not a work-cap result. Their exact shared analysis mechanism and diagnostic are verified by private N/N-1 through both public adapters.',routes=['archive','shared','author prebuilt','ASan+UBSan']),loose_bound='candidate2-LOOSE_WORK_BOUND.json'),
 ledger=dict(receipt='candidate2-LEDGER_STACK.json',records=23,exact_profiles=3,complete_GCC_units=14,complete_GCC_frames=145,wrapper_bytes=stack['stack']['wrapper'],sum_every_nonwrapper_frame=stack['stack']['sum_all_other_frames'],fixed_combined=stack['stack']['fixed_combined'],job_combined=stack['stack']['job_combined'],scope='Conservative GCC14.2/Linux x86-64 C-owned upper bounds, not observed peaks; external libc/libm/OS/caller/sanitizer frames excluded.'),
 target_accounting=dict(receipt='candidate2-TARGET_COUNTS.json',format_exhaustion_control='candidate2-ID_BOUNDARY.log',scope='Exact header highest-ID+1 and selected opcode/root word counts on11 already-emitted witness instances, plus retained format-saturation boundary control. Parent owns SPIR-V validation/interleaved execution and full corpus.'),
 historical_mappings='HISTORICAL_MAPPING_BEFORE_ADAPTERS.json; original142 retained inputs verified unchanged, only enumerated accepted-M10 overlays reused. Parent owns the exact newly admitted workgroup source and exact private-global status mappings.',
 qualifications=['No remaining unqualified native gate within this host/compiler scope.','Not a claim of GPU/hardware execution, another host ABI, TSan race proof, performance parity, runtime integration, or whole-repository acceptance.','Author receipts remain separate; independent source/prebuilt hashes and routes are explicitly distinguished.','No invalid full-capacity aliases were used to invent an overlap-rejection obligation.'])
save('NATIVE_VERDICT.json',receipt)
