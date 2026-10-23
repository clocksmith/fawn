# Physical storage, phase and work ledger

Frozen design only. `contracts/m13_layout_probe.c` and its `.txt` output are an isolated
ordinary-GCC layout calculation, not compiler code, tests, measured compiler stack or
acceptance. Current source and both M12 candidate identities are in contracts/m13_source_inputs.json.
Accepted M12 commit `e8c1b2ecb1f94bf0a8cf871835fed9038a0bc572`, tree
`624939bef7c91b3dc8623f3995d6b98fb393d0e6`, binds those dependencies. No apparent
spare stack is available.

## Units and exact slice populations

Retain M10/M11 symbols: source bytes H, tokens T, source expressions E, physical
expressions Ep, statements S, types Q, members M, resources R, access steps X,
constant nodes K, constant edges L, IR I, operands O, target descriptors D,
type traversal V, staged uint32 words W. All are physical capacities, not counts
in an emitted target or permanent language maxima.

Derived slices, not additional user-sized pools:
- Tokens T+1 (end token). Ep=E+1 normally; fixed Ep337 retains all exemptions.
- ConstantSyntax E+1; Function F=floor(T/6); helper Formal P=floor(T/4);
  Argument E; GenericSyntax TS=floor(T/2), except old compact TS32.
- Workgroup roots G=floor(T/8), from eight tokens in the shortest complete root.
- Source bindings S+1; IR locals S+1+X; semantic value map I+P+1; type map Q+7.
- SourceReference Ep expression slots plus S statement-root slots, each8 bytes.
  A reference is a typed kind/index pair, NOT an allocation per repeated use,
  call execution, array element, graph edge copy or target instruction.
- Graph scratch: R+G mark bytes, plus aligned uint32[R] for collision sorting.
  Function graph cursor/parent/color uses existing FunctionRecord state.
  No J×F/R/G mark matrix or unbounded adjacency arena.

ABI5 adds only three explicit logical pools: J entries, B aggregate entry inputs,
C flattened entry-reference slots. All zero capacities are valid queries. No new
function/formal cap is needed. Actual entries<=floor(T/13): the shortest admitted
entry `@compute @workgroup_size(1) fn a(){}` has13 tokens. Aggregate inputs<=floor(T/8)
and <=5J: each shortest admitted builtin parameter costs8 tokens and a valid entry
cannot duplicate one of the five admitted builtin kinds. A malformed prefix may fail
its checked reservation before a grammar diagnosis; it can never consume unchecked
storage or become accepted. Inputs append only complete parameter records.

C counts the sum over all entries of unique resource counts plus unique workgroup
root counts. Within each entry, append resource indices in declaration order, then
workgroup indices in declaration order. Record both ranges. No duplicated binding
metadata, per-element workgroup data or target IDs enter this pool. Actual C<=J(R+G),
but ABI5 need only allocate its explicitly chosen sparse C, not that Cartesian bound.
Every attempted append checks C before write. C exhausted: pool22, minimum=attempted
extent, INPUT_ERROR at active entry. B/J use pools21/20 similarly. Exact output
entry_references equals actual private C, independent of which entry was selected.

Compatibility adapter capacities:
- Fixed ABI1/v2 and old job: J1, B5, C=R+G. A second entry is an unrepresentable
  singular projection, not an implicit selection. Other physical caps unchanged.
- ABI4: J=floor(T/13), B=floor(T/8), C=min(UINT32_MAX-1, J*(R+G)), with checked
  arithmetic. This supplies every representable closure; saturation is the public
  uint32 reference-index format, not a silent truncation. Any actual format overflow
  rejects UNSUPPORTED. The requirement query has no flags and therefore must reserve
  this module-capable layout even when a caller later requests singular reflection.
- The unchanged binary CLI retains its exact T/E/S/Q/M/R/X/K/L/I/O/D/V/W/depth
  profile and calls ABI4 WORDS. Its workspace query grows; no profile auto-retry.
  ABI5 is the explicit sparse-memory alternative, not a second parser/emitter.

Original fixed caps: T2048,E320,Ep337,S16,F341,P512,G256,Q32,TS32,M64,R8,
X337,K64,L128,I385,O320,D464,V97,W4096, depths18/4/18.
Binary CLI caps: T8192,E4096,Ep4097,S1024,F1365,P2048,G1024,Q256,TS4096,
M1024,R32,X5121,K1024,L4096,I24577,O4096,D7744,V1281,W65536, depths32/16/32.
Thus ABI4 CLI derives J630,B1024,C665280. Report this cost rather than hiding it.

## Planned records and lifetime proof

Probed x86-64 C11 size/alignment: core480/8, limits112/8, Entry64/8,
EntryInput20/4, SourceReference8/4. Existing Function80/4, Formal16/4,
Expression40/4, Statement56/4, Type56/8, Member56/8, Resource104/8,
WorkgroupRoot24/4, IR32/4, TargetDeclaration32/8 and Walk16/4 stay unchanged.
Function formal_start/entry_index is a tagged union of uint32, not added storage.
Entry owns function/input range/static input mask and one phase union:
- parse: three Integer attribute records, including original spans/expressions;
- resolved: three dimensions, resource/workgroup ranges, local-index target,
  target input mask and checked uint64 logical workgroup bytes.
Do not overwrite parsed dimensions until every attribute value is fully resolved;
save three scalar values in bounded temporaries before publishing the resolved state.
EntryInput keeps source name/kind/type-spelling flags plus its target ID. No embedded
per-entry five-input array. Fixed single-entry adapters reuse the same records.

For every slice: checked align_up(cursor,alignof(record)), checked count*size, then
checked add. Round phase starts/total to their maximum alignment. Layout query and
binding use one owner; no stable private offsets are public. Name/source offsets fit
uint32; every count reserves its NO_ID sentinel; Q+7, I+P+1 and S+1+X are separately
checked. Ep+S, R+G and J(R+G) use checked size/uint64 arithmetic before narrowing.
Extra caller workspace never raises logical caps. No allocation or VLA is introduced.

Persistent ordered slices:
Core; Expression[Ep]; Statement[S]; ConstantSyntax[E+1]; Function[F]; Formal[P];
Argument[E]; GenericSyntax[TS]; Type[Q]; Member[M]; Resource[R]; WorkgroupRoot[G];
Constant[K]; ConstantValue[L]; Access[X]; Entry[J]; EntryInput[B]; uint32[C].
The former persistent uint32[Ep] callee-only map is removed, not retained in parallel.

Phase A: Token[T+1]. It dies completely after parse.
Phase B common: IR[I]; Local[S+1+X]; Binding[S+1]; uint32[I+P+1] values;
uint32[O] operands; byte[S] statement behaviors; aligned scratch start.
B-analysis: SourceReference[Ep]; SourceReference[S]; then max(U, Gscratch).
  U = byte[Ep], byte[S+1], byte[P], byte[F], aligned 32-byte frames[A],
      byte[6*A*(S+1)], with A=2*control_depth+expression_depth+4.
  Gscratch = byte[R+G], aligned uint32[R].
B-target: uint32[Q+7] reflection map; TargetDeclaration[D]; Walk[V]; uint32[W].
Total = persistent + max(A, B-common + max(B-analysis, B-target)), including
actual ordered alignment padding. The probe computes offsets, not merely sums.

Legal sequence: parse → full sema/reference capture → source cycle checking →
all-entry closure building → uniformity → target plan/emission → reflection prepare →
selection/projection/capacity/FP restoration → publication. Closure may precede or follow
uniformity, never overlap its scratch live interval. Both finish before targets overwrite
source-reference/graph scratch; immutable closure indices persist through publication.
Entry/function target IDs are assigned only after graph state dies. Source AST/types,
IR/accesses, declared tables, source name spans, selection input and copied diagnostics
remain valid through publication. Reflection needs no source-reference/call-graph pass.
A separate after-emit closure recomputation is prohibited by output atomicity.

## Isolated layout results

No compiler library was built or called. GCC compiled only the record/layout probe.
Proposed public sizes on this ABI: capacities160, selection24, workgroup32, entry64,
counts64, outputs168, module16, result376 bytes. Public source names/records remain
ordinary typed fields; these are local C ABI observations, not serialized byte formats.
Planning fixed singular: persistent92256 + common20856 + target32944 =146056 bytes.
Its token alternative24588 and analysis2824+max(5232,296)=8056 are smaller.
Current fixed146256 is200 bytes larger. Existing147456 workspace ceiling need not
rise for this proposed layout; this is not proof the implementation will fit.

ABI4 unchanged binary profile: persistent3701336 + common1008744 + target531504
=5241584 bytes, versus historical2536128. Analysis40968+max(428912,1184)=469880,
still below target scratch. The growth is explicitly the all-entry representation
and conservative closure slice, not a new grammar or hidden library allocation.
ABI5 illustrative SAME base pools with J32/B160/C2048:2533104 bytes. This example
is a layout demonstration, not a selected CLI policy or assurance every shader fits.
Native query/size/alignment/reuse/one-short evidence must validate actual changed code.

## Names, target IDs, words and format limits

No separate private name copy: source NameSpans survive the call, and public names are
caller-owned output. Exact N=sum(length+1) over every entry, named reflected type,
reflected member, resource and (V5) workgroup. Unnamed scalar/anonymous types consume0.
N<=64*(J+named_struct_count+M+R+G) under current63-byte admission, calculated checked;
actual N and all offsets must fit uint32. Output N-1 is atomic OUTPUT_TOO_SMALL.
No entry×name matrix. Serialize target strings directly into checked staged words,
not a module-size automatic array. String words n(s)=floor(length/4)+1 include NUL.

Exact changed target rows (word counts include opcode):
- EntryPoint: 3+n(name)+b words, zero IDs; b is that entry's emitted input interface.
- LocalSize execution mode:6 words, zero IDs, correct target function identity.
- Entry OpName:2+n(name) words, zero IDs.
- Source input variable:1 ID/4 words, BuiltIn decoration4, debug name2+n(name).
- Hidden LocalInvocationIndex:1 ID/8 variable+decoration words, one interface operand.
- One used workgroup root declaration:1 ID/4 words; debug name<=18 words. All declared
  roots may remain declared; only closure roots receive entry initialization stores.
- Prologue for an entry with roots:4 IDs/26 words plus3 words per closure root.
  Typed-zero/pointer descriptors use existing D; shared scope/semantics constants
  remain bounded shared declarations, not one duplicated declaration per entry.

All existing M12 admitted IR rows remain bounded by64I IDs /256I words, pending
accepted M12 identity. Let Cr/Cg be actual reference counts, Cg<=C; Z<=J be entries
with nonempty workgroup closure; Bactual<=B and hidden Hinit<=Z. Exact added input
IDs=Bactual+Hinit, prologue IDs=4Z. Existing helper formal IDs remain P, not B.
Semantic value map remains I+P+1; generated target temporaries never index it.
A safe conservative replacement envelope is:
  ID_bound <= 256+D+64I+3F+P+(S+1+X)+10X+R+G+B+5J+8.
  words <= 2048+256I+32F+4P+O+9D+6M+L+4K+57X+40R+22G
           +96J+32B+3C+64.
Overcounted shared/single-entry terms are intentional safety slack, not measured use.
D's envelope remains7Q+2R+K+L+M; source inputs/roots use record target fields, not D
per variable. Actual D/W appends decide success. Evaluate envelopes with checked
uint64 operations; never wrap before saturating the ID ceiling at UINT32_MAX-1.
Output header records actual highest target ID+1. Word capacity and format capacity
are separate. Format failure is UNSUPPORTED, never a fabricated growable pool.
Every streamed instruction independently requires word_count<=65535. EntryPoint
requires 3+n(name)+b<=65535; helper signatures/arguments, structs and composites
keep their existing checks. Never invoke newer SPIR-V interface rules accidentally.

## Explicit graph work and composed stack

Per-entry traversal visits each reachable function once, its expression slots once,
its assignment-root slots once; scans R+G marks and stores at most R+G references.
Collision sorting is an iterative heap sort of at most R IDs; no source spelling,
array-length flattening or host recursion. With Lr=ceil(log2(max(2,R))), a conservative
new owner work bound is J*[20*(F+Ep+S+R+G+1)+8*R*(Lr+1)], plus the existing cycle walk.
A work unit is a scalar slot inspection/write, resolved-reference visit or constant-size
binding-key comparison/swap; implementation must bind its accounting to this algorithm.
Use checked uint64 count/bound arithmetic; an unrepresentable actual bound is an explicit
UNSUPPORTED work-format failure, not partial acceptance. This derived finite bound is
not an arbitrary new shader-specific cutoff. Report actual graph/closure counts and
adversarial scaling in evaluation. M11's separate2^26 uniformity operation limit remains
unchanged, including its complete sensitivity/fixedpoint accounting and no-barrier rule.

No new graph/entry recursion; entry descriptor parsing, closure/collision processing and
publication are iterative. Preserve all accepted stack ceilings, not just frame sizes:
fixed workspace<=147456, ABI1 wrapper<=147968, caller-workspace wrapper<=1024;
other individual frames<=8192 and non-wrapper acyclic path sum<=16384.
Recursive SCC sums: parser expression/type512, parser control256, semantic expression1536,
semantic control768, emitter384, uniformity expression/control256. Phase bounds remain:
parser512*(Dexpr+Dtype+2)+256*(3Cdepth+4);
sema1536*(Dexpr+2)+768*(3Cdepth+4);
emitter384*(2Cdepth+Dexpr+4);
uniformity256*(Dexpr+2)+256*(3Cdepth+4).
Combined = max(phase)+acyclic+wrapper: fixed<=212992, max-job<=196608.
These are requirements, not new measurements. Every compiler call path must be checked.

M12 candidate2 report: normal semantic SCC1520/1536 and other-frame sum16368/16384;
PIC1488/16264. The FULL semantic SCC includes expression, postfix, call, builtin,
type_reference and integer_attribute. The earlier author subtotal was incomplete.
No new entry/reference helper may assume spare recursive/acyclic stack. A64-byte
Entry automatic temporary or extra helper path can break a budget despite the smaller
workspace. Prefer direct caller-slice construction and bounded scalar temporaries;
prove actual .su, SCC membership, composed path and normal/PIC shapes after implementation.
If unavoidable growth is found, report exact cause and seek a revised contract before
freeze/implementation, not a late ceiling increase or test reclassification.
