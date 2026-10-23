# M11 concrete storage, uniformity and target ledger

Frozen implementation ledger, 2026-10-23. Read MILESTONE_11.md. Exact accepted M10 input is bound by
contracts/m11_source_inputs.json. contracts/m11_layout_probe.c uses accepted record declarations and only adds
planning records; it is NOT compiler code. No implementation/test/header modified.

## Source-backed storage and unchanged public capacities

Use T,E,S,I,Q,M,R,X,K,L,O,D,V,W as M10 does; F=floor(T/6), P=floor(T/4).
All source bindings (mutable, let and local const) use Ls<=S+1 slots. Workgroup root allowance G=floor(T/8): even the
shortest complete `var<workgroup> x:u32;` consumes eight tokens. Append after
consuming a complete declaration, never reserve one root per malformed prefix.
No new public cap/status/enum/export. Workgroup syntax charges ordinary tokens,
its annotations/extent expressions charge existing Q/type-syntax/E pools, and
actual root count cannot exhaust G while token accounting is valid.

Proposed private root: NameSpan name; TypeRef annotation; TypeId type; u32 target.
Measured 24 bytes/alignment4, no inline array payload or descriptor coordinates.
Core adds root pointer, uniformity-scratch pointer, root count, flags and u64
checked logical workgroup-storage total, plus a statement-behavior pointer: 552 bytes/alignment8 (M10:512).
All other accepted records remain same widths; a separate byte[S] records existing
sema-computed statement behavior. New root kind/opcode fit private enums; no public
enum is repurposed.

No fixed global count or flattened backing data proportional to array extents.
An array of 2048 f32 values still consumes one root, an existing type record and
compact zero, not 2048 compiler data records. Type/offset/stride arithmetic remains
checked u64; target narrowing and instruction/ID limits stay separate. The
logical total is checked sum of each declared type size rounded to its natural
alignment; it is bookkeeping, NOT a device's physical shared allocation size.
No undocumented WebGPU device limit or new universal workgroup-byte ceiling is
invented in this independent compiler. Physical-limit validation is a runtime gate.

## Uniformity dependency graph without an adjacency arena

The normative logical graph is represented by the retained validated source AST,
NOT folded runtime IR. Expression/Statement/ArgumentSyntax tables and NameSpans
already remain alive. Constant-skipped RHS operands can have no emitted IR, but
are still visited in the AST under the LHS CF as required by dated WGSL15.2.8.
For example, `false && sync()` inside varying control still inherits sync's
uniform-callsite requirement; `false && local_id.x>0u` remains a varying condition
for analysis. The emitter continues to skip the runtime RHS, unchanged.

Resolution is not guessed by spelling. The existing sema call resolver records
all calls, including emit=false visits, in the existing callee_map[physicalE]:
0 means non-user/non-barrier (typed constructor or admitted pure scalar builtin),
1..function_count means a resolved user function, and UINT32_MAX means the
resolved builtin workgroupBarrier. Update call-cycle traversal to distinguish
that marker from a function index. A struct or user function named like the
builtin therefore does not acquire barrier semantics. The no-barrier fast path
checks these resolved markers, not emitted opcodes or source text.

During each source pass, reconstruct lexical binding VIEWS using the existing
LocalScope storage and lookup helpers, respecting declaration-after-initializer,
shadowing, for-header/body/continuing scopes and source visibility. Bind each local
to its original Statement index (at most S), not an IR/target ID. Its runtime
uniformity snapshot lives in current_local[index]. Lets and local consts are
immutable bindings, vars mutable; whole-root versus partial assignment uses the
already validated source Assignment root/path. Formal/module/input/root lookup
uses current typed module records. Every admitted ExpressionKind is covered, including the existing compact legacy
root/member/index representations. No type parsing, overload selection, numeric
folding, IR append or semantic revalidation occurs in this pass. If the existing
helpers couple lookup to lowering, factor a shared read-only identity helper;
do not duplicate a conflicting semantic resolver. Module const values are uniform
const-stage sources; named constructors/pure builtins propagate source arguments.

Save lower_block's already-computed behavior for EACH source Statement in byte[S]
in common lowering storage. For/while/loop behavior includes the existing header,
body/continuing rules; graph traversal uses these exact bits instead of guessing
from emitted labels. Add no second behavior-analysis definition. All name/type
validation (including dead statements) finishes before uniformity, whose graph
alone ignores no-Next suffixes per the normative rule.

Resolve call order iteratively with existing FunctionRecord graph links. There
is no C recursion over helper/callee depth. Function summaries remain separate
from value bits and are complete before caller analysis.

For one function, solve reachability one distinguished source at a time using
one-bit dependencies and OR joins. RequiredUniform and return are sink bits:

- N run: MayNonUniform=1; entry CF and all formals=0. A required bit means
  unconditional uniformity failure; return bit means intrinsically varying result.
- C run: entry CF=1; MayNonUniform and formals=0. Required bit sets uniform-callsite
  summary. Caller result always includes caller CF, so no separate CF-return bit.
- One P_i run per formal: only formal i=1. Required bit sets that formal's uniform
  argument requirement; return bit sets its return-dependency flag.

Exactly formal_count+2 sensitivity runs occur per function when analysis is needed,
so the whole module has at most P+2F runs. No formal subset is guessed away.
This is distributivity of OR dependency equations, not runtime shader execution.
All actual callee summary bits are fixed before caller runs. During each run,
intrinsic callee-return variability is seeded ONLY by that run's MayNonUniform
source, not unconditionally true (otherwise formal/CF sensitivity is corrupted).
Likewise a direct varying builtin is 1 only in N. Do not carry N-run bits into
later runs. Store summaries as two meaningful bits per function and per formal.
A module without any resolved synchronization builtin can skip this pass; it has
no uniformity requirement in the admitted compute language. This is a proof-based
fast path, not a spelling search; it preserves large no-barrier formal/const cases.

Structured transfer retains current source-binding bits and six local-bit snapshots at
an active control frame: incoming, first branch, loop head, joined continuing,
joined break and temporary merge. Normal/current state is separate. Each frame
also reserves 32 bytes for its source/control/behavior/loop fixpoint state.
Expression scratch bits are indexed by original physical expression ID. Ls uses
source Statement identities, including immutable snapshots, never Function-spill
slots or target IDs. Existing LocalScope records are reused only after sema dies.

A whole-local store replaces its bit; partial store ORs old/RHS/index/CF. On a
loop visit, start head from incoming state; derive body/continuing, join only live
backedges, and repeat until head local/CF bits stabilize. Head bits only grow.
Lattice has at most Ls+1 head bits, hence at most Ls+2 body transfers per loop
invocation/seed, including final stable transfer. Required/return/break-exit bits
accumulate across those transfers. Never use an arbitrary iteration count and
never treat a partial run as proof. Nested loops solve their own least fixed
point per outer transfer; the finite depth gives a finite, potentially expensive
bound. This design makes no compile-speed claim; collect transfer counts and
host time in acceptance, including large-formal and nested-loop controls.

Freeze a shared per-compilation work ceiling of 67,108,864 dependency operations
(2^26), identical for every public adapter. Charge BEFORE each source-expression/statement visit, each local-map byte
copied/compared/joined, each value/scratch slot reset, each operand/access-step/
summary/formal inspected, each lexical/module name comparison and each call-graph
edge visited. Use a checked uint64 counter; a step that would exceed the
ceiling does not execute. This is an explicit bounded analysis capability, not a
fixedpoint iteration shortcut. Exhaustion returns UNSUPPORTED, source offset of
the active original operation/function, message "uniformity analysis work limit
exceeded", exhausted_pool=NONE/minimum_pool_capacity=0; publishes no output.
It must never return semantic-nonuniform or accept a partial solution. The no-
resolved-barrier fast path performs no sensitivity runs and does not impose this
new work ceiling on any prior successful no-barrier program.

A conservative shape bound for one seed is
(physicalE+E+2X+P+F+S+32)*(6*Ls+32)*(Ls+2)^C_f, times P_f+2 runs
for that function, where C_f is maximum active loop nesting (not call depth).
Using module capacities also covers expression-map resets; E bounds the syntax
argument links of high-arity calls even with few emitted IR records. Charge each
lexical/module name comparison too; multiplying the loose bound by (S+P+F+R+G+Q+1)
covers worst-case linear identity lookups. This estimate is not an admission limit. Compute it with saturating checked u64
only for receipts,
never reject a source solely because the loose estimate saturates. Actual counted
work governs exhaustion. Exact/one-step-short private-budget controls must verify
the threshold without exposing a new export. Record observed counts for all six
mandatory witnesses and larger helper/control probes before acceptance; change
the ceiling only by reviewed pre-candidate amendment, not after a failing case.

The analysis follows exact behavior channels, not numerical path feasibility.
Do not analyze dead suffixes into the graph, although prior sema validated them.
For if/loop {Next} reset outgoing CF to incoming CF. In all other cases apply the
dated graph's CF equations. Mutable local maps still join the appropriate exit
values, so a CF reset does not make a divergent assigned value uniform. `for` and
`while` use the same validated source desugaring semantics, not separate analyses.

If an implementation uses a worklist instead, its storage must be contained in
the same frozen slices and its exact edge/node/queue bound reviewed BEFORE it
replaces this solver. No hidden heap, fixed global graph arrays, parameter×node
bit matrix, silent iteration cap or broader allocator framework is authorized.

## Exact slice order and lifetime

Each append uses checked align_up + checked count*sizeof + checked sum.
All new derived sizes use size_t/u64 checks without shrinking old query admission
on a new conservative target-ID estimate. No u32 cast of products before checks.
G itself fits existing token domain. H=2*control_depth+expression_depth+4 safely
bounds active source control/join
frames, including nested short-circuit context. AST expression recursion has its
separate unchanged expression-depth bound; it does not recurse through type DAGs.

Persistent: Core; Expr[physicalE]; Stmt[S]; ConstantDecl[E+1]; Function[F];
Formal[P]; Argument[E]; callee_map[physicalE]; GenericSyntax[TS]; Type[Q];
Member[M]; Resource[R]; WorkgroupRoot[G]; Constant[K]; ConstValue[L]; Access[X].

Main phase union (max of A and B):
A. Token[T+1].
B. Common lowering: Ir[I]; Local[S+1+X]; Binding[S+1];
   u32 semantic_map[I+P+1]; u32 operands[O]; byte statement_behavior[S]; align8 then a secondary union:
   B1 target/reflection: u32 reflection_map[Q+7]; TargetDecl[D]; Walk[V]; u32 words[W].
   B2 uniformity: byte expression_bits[physicalE]; byte current_local[S+1];
      byte formal_summary[P]; byte function_summary[F]; align4 then
      eight-u32 FrameMetadata[H] (32 bytes each);
      byte frame_maps[H*6*(S+1)]. Finally align8 the union size.
      Metadata and packed byte maps are DISTINCT aligned slices, not134-byte C
      structs whose natural alignment would silently introduce padding.

Tokens die at parse return. AST/IR/types/access/operand/function information persists.
Uniformity is complete before ANY target declaration/word planning; B1 may then
replace B2. No reflection map/target/word/walk data exists while B2 is active.
Summary bytes need not survive this pass; reflection/target do not consult them.
Current scalar sema using reflection/target scratch must be audited: no phase
reuse is allowed until all users are proved dead. Typed resolver uses Walk during
sema; it finishes before uniformity, so Walk -> B2 -> Walk is sequential reuse.
This is genuine phase union accounting, not pretending relocated storage vanished.

Checked standalone probe, GCC14.2/Linux x86-64, C11 -O2 -fstack-usage:
- Fixed: persistent92456, token24588, common20856, target32944,
  uniformity5232, TOTAL146256/alignment8. Accepted M10 total140056; delta6200.
- Binary CLI: persistent995880, token98316, common1008744, target531504,
  uniformity428912, TOTAL2536128/alignment8.
- Old-job with CLI source/T/E/S/W/depth and compact M10 aggregate caps:
  persistent735336, common1008744, target278704, uniformity428912,
  TOTAL2172992/alignment8. Here uniformity, not words, determines scratch peak.
Probe source/output are retained; future actual implementation still must measure.

Fixed adapters keep Q32/TS32/M64/R8/X337/K64/L128/O320/D464/V97, T2048/E320/S16,
I385/W4096, physicalE337, depth18/4/type18. D remains an explicit append limit,
not a promise all maximum shape combinations fit. Old job keeps exact old public
capacity admission/formulas and compact aggregate caps. ABI4 keeps all explicit
pools unchanged. Binary CLI stays the exact M10 profile; no auto-growth/retry.
Adding roots does not reduce descriptor R. Every old successful no-root source
has identical logical pool use, so extra workspace is not a new source rejection.
Current-library queries own private size changes. Stale hardcoded M10 byte spans
reject before use; separately derived regression mappings update exact private
size assertions, not historical headers/receipts or source language budgets.

## Stack bounds

Keep the ALREADY REVIEWED M10 safety ceilings: fixed workspace<=147456, ABI1
wrapper<=147968, other individual frames<=8192; non-wrapper acyclic path sum<=16384.
The proposed fixed layout fits with1200 bytes spare. Do not add another full
workspace or recursion-proportional automatic array. Caller-owned APIs keep their
wrapper<=1024. Proposal layout is not a measurement of the eventual wrapper frame.

Existing recursive-family ceilings remain: parser expression/type512,
parser control256, sema expression1536, sema control768, emitter384.
Proposed uniformity source-expression recursive SCC sum<=256 and source-control
SCC sum<=256, with active depth bounds Dexpr+2 and3C+4 respectively. All local maps
are in B2, not on C stack. Its phase contribution<=256*(Dexpr+2)+256*(3C+4):
9216 fixed,42496 at maximum depths64/32. This stays below existing phase maxima.

Combined bound remains max(parser, sema, emitter, uniformity)+acyclic+wrapper.
Parser512*(Dexpr+Dtype+2)+256*(3C+4); sema1536*(Dexpr+2)+768*(3C+4);
emitter384*(2C+Dexpr+4); uniformity256*(Dexpr+2)+256*(3C+4).
At fixed: max43008; at maximum job: max178176.
With wrapper ceilings:207360 fixed and195584 job, within unchanged rounded ceilings
212992 and196608. Derive again from actual `.su` and complete call graph after
implementation. Excludes libc/libm/OS/sanitizer/caller frames; NOT observed peaks.

## IR and SPIR-V exact additions

One new ROOT_WORKGROUP in the typed reference category and one IR_WORKGROUP_BARRIER
(no value, no operands) suffice. Root accesses keep the existing ACCESS_LOAD/STORE
and paths. Barrier calls charge one actual semantic IR append; zero-prologue
expansion belongs to target lowering from validated roots, not E source nodes.
Existing sema also records statement behavior in the new common byte[S] slice.
IR_LOOP_BEGIN already carries its runtime whole behavior; no new public IR is
introduced and no runtime IR is fabricated for skipped source operands.

Target declarations: one extra pointer storage-class variant per used type,
including primitive Workgroup pointers in the fixed allowance. Existing typed
zero descriptor can be shared. A safe dynamic descriptor upper envelope is now
7Q+2R+K+L+M (previous6Q...). Actual public D remains append-checked and explicit.
No descriptor per scalar element. Root OpVariable IDs are tracked in root records,
not charged twice as target descriptors. Need no Block/group/binding decorations.

New rows, counts INCLUDE opcode and labels/results:
- Workgroup OpVariable:1 ID/4 words per root; optional max63-byte name<=18 words.
- Reused typed OpConstantNull:1 ID/3 words per distinct typed zero, charged D.
- Workgroup OpTypePointer:1 ID/4 words per distinct type, charged D.
- Whole-root prologue OpStore:0 IDs/3 words per root.
- OpControlBarrier:0 IDs/4 words, scope constants2/2, semantics0x108.
- Hidden LocalInvocationIndex variable if necessary:1 ID/4 words plus4-word
  BuiltIn decoration and1 interface operand. No debug-name obligation.
- Entry prologue body outside stores: load1/4, equality1/5,
  SelectionMerge3 words, BranchConditional4, two labels2/4,
  branch-to-merge2, final barrier4:4 IDs/26 words.
  No else label is needed: false branch targets merge directly.
- At most two extra scalar u32 constant declarations (scope2 and0x108):2 IDs/8
  words, inside the retained fixed256 declaration/guard allowance. Reuse2 if present.
- Generated input reuses source LocalInvocationIndex when present. Since five
  source builtin kinds are unique, missing that kind means<=4 source parameters;
  augmented target interface still<=5. Do NOT mutate source parameter_count.

Extend M10 conservative bounds by G+8 target IDs and25G+64 words. Source barrier
rows are already dominated by64I/256I; the extra covers roots/names/stores,
prologue, constants and hidden input. Thus:
IDs<=256+D+64I+3F+P+(S+1+X)+10X+R+G+8.
Words<=2048+256I+32F+4P+O+9D+6M+L+4K+57X+40R+25G+64.
Exact reservations/checks still decide actual success, target header bound stays
actual highest ID+1; saturated target format exhaustion remains UNSUPPORTED.

## Failure and regression map

No partial words/reflection on any error; preserve published=0, diagnostics,
current required-count exactness rules, all alias/span/fenv/errno ownership. All
output modes run uniformity and full private emission with the SAME capacities.
- New grammar admitted: module workgroup vars; builtin workgroupBarrier calls.
- New real rejection: normative nonuniform synchronization, original source site.
- Existing type-invalid local/global/initializer/access use remains semantic error.
- Valid excluded type/builtin/target capability remains UNSUPPORTED.
- Existing named capacity/output/target-format taxonomy remains unchanged.
- Derived root/scratch count is fully reserved by the current workspace query;
  corrupted/internal overflow is input/layout failure, never truncated graph success.
- Exact private size tests are rebound to loaded-library query. Original accepted
  headers, exports, success cases, raw historical tests and receipts are preserved.

## Authoritative references and qualification

WGSL fixed dated CRD: https://www.w3.org/TR/2026/CRD-WGSL-20261021/ .
SPIR-V official unified spec consulted2026-10-23, header version1.6/revision8:
https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html . Target remains1.3;
only core Workgroup/OpConstantNull/OpStore/OpControlBarrier rules are used. Official
separate1.3 URL was inaccessible during planning; no version-date is fabricated.
Future receipts must bind installed validator hash/version/options. No tools were
installed/downloaded and no GPU execution was performed by this planning pass.
