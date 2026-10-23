# M10 frozen storage and lowering ledger

Frozen contract ledger. Read with MILESTONE_10.md. The source at
contracts/m10_layout_probe.c is a standalone record-layout probe, not candidate
compiler code or a candidate test. Original planning revisions and measurement
receipts remain unchanged in the isolated milestone10/planning evidence.

## Measured layout, not claimed implementation performance

Reproduce from runtime/c/compiler with GCC Debian14.2.0-19 on Linux x86-64,
placing generated outputs outside source:
`gcc -std=c11 -Wall -Wextra -Werror -O2 -fstack-usage contracts/m10_layout_probe.c -o "$OUT/layout_probe"`
then `"$OUT/layout_probe" > "$OUT/layout_probe.txt"`. The compiler also writes
a stack-usage receipt beside its output. Original probe output reported the
record/frame values below; this is not future implementation evidence.

All record declarations are in contracts/m10_layout_probe.c. Payload arrays there stand for
closed typed union alternatives of that exact maximum size; they are NOT a
proposal for erased domain dispatch. The diagnostic pointer is likewise a
layout-only stand-in for DoeCompilerDiagnostic*. Production declarations must
name typed alternatives and fields. Source offsets/IDs are validated u32.

Measured size/alignment in bytes:

| Record | Size | Alignment | Owner |
|---|---:|---:|---|
| Core |512|8| capacities, typed slice pointers/counts, entry inputs |
| Expr / Stmt |40 /56|4| retained AST and source positions |
| ConstantDecl |40|8| inline scalar value OR aggregate ConstId, resolver links |
| Function / Formal / Argument |80 /16 /8|4| same M9 ownership/ranges |
| GenericSyntax |24|4| generic annotation only, not every scalar spelling |
| Type |56|8| nominal/composite shape and one checked layout |
| Member |56|8| source annotation/attributes, resolved type/layout |
| Resource |104|8| syntax, validated root type/range metadata, target identity |
| Constant / ConstValue |32 /16|8| compact aggregate DAG / inline scalar-or-ID |
| Access |24|4| checked typed source access step |
| Ir |32|4| concrete value type, source offset, closed four-word payload |
| Local / Binding |8 /48|4 /8| Function slot / source-linked lexical binding |
| TargetDecl / Walk |32 /16|8 /4| target descriptor / explicit traversal work |

Primitive TypeIds0..6 encode void/bool/i32/u32/f32/AbstractInt/AbstractFloat;
they need no Type records. Other IDs are7+index in the Q pool. TypeRef is8 bytes:
source offset plus generic-syntax index or NONE. Resolve the original spelling
through scope before mapping to a primitive, so compactness does not bypass
shadowing. Repeated scalar/nominal annotations retain TypeRefs, never allocate
new shape records. Intern composite shapes only after resolution commits.

ConstantDecl stores ordinary scalar bits inline, as M9 does. K counts only
aggregate constant nodes; L edges are16-byte ConstValues containing an inline
scalar value or a child ConstId. A scalar-only old program consumes K=L=0.
Copies share immutable aggregate nodes. ZERO costs one node, no edges regardless
of array length. REPEAT is used only for vector splats (count<=4) in this owner;
arrays with nonzero contents use explicit COMPOSITE child edges, not speculative
compression followed by uncharged target expansion. Struct/array materialization
creates committed converted nodes keyed by source ConstId and destination TypeId.
No full scalar-leaf expansion or speculative losing type/node insertions.

## Exact compatibility profiles and preservation proof

For fixed ABI1/v2 retain T2048,E320,S16,I385,W4096,physical expressions337,
F341,P512,source65536,depth18/4. New compatibility budgets are:
Q32 aggregate/nominal types; TS32 generic syntax; M64 members; R8 resources;
X=E+S+1=337 access steps; K64 aggregate constants; L128 constant edges;
V=Q+K+1=97 walk items; O=E=320 typed operands;
D=6Q+2R+K+L+M=464 dynamic target descriptors.

Old-job adapter retains its old source/T/E/S/W/depth admission and exact legacy
query overflow checks. F=floor(T/6), P=floor(T/4), physical expressions=E+1,
I=1+4E+8S, module constants=E+1. Q,TS,M,R,K,L,V,D are the SAME compact constants
above, not Q32+T. X=E+S+1 and O=E. New aggregate programs exceeding these named
pools fail capacity explicitly; ABI4 exposes larger pools. Extra physical bytes
do not expand any limits. This is capacity, not a second grammar.

Why no old successful program is lost:
- Eight nominal flat structs plus one shared input vec3 and two scalar storage
  array shapes use at most11 aggregate shapes, below Q32. Their scalar members
  are primitive IDs. Primitive types and guard constants are not charged Q.
- Old generic annotations are at most five builtin inputs plus eight resource
  array annotations, hence at most13 TS records, below32. Arbitrarily many
  scalar helper formals need no TS/Q records, including M9's65532-formal case.
- Old structs total<=64 members and resources<=8. No old scalar const needs an
  aggregate node/edge, even with long const chains or many const initializers.
- Each old resource/member read has its own source expression; each assignment
  adds at most one LHS access. Hence X=E+S+1 covers old access metadata including
  the single historical exempt expression. Compound load and store SHARE the
  once-evaluated path/index records; they do not append it twice.
- Calls' explicit operand roots remain charged once to E. Function/formal counts,
  signature streaming, original scalar IR385/four-expression/eight-statement
  bounds, word budgets and source accounting exemptions are unchanged. Bigger
  target expansion is not charged as extra scalar semantic IR.
- Module const/function graph walks still use embedded iterative cursor/parent
  fields, not V items proportional to their arbitrary declaration-chain length.
  Tagged parent kind lives in state flags; parent index remains a full u32.
  Type graph color/cursor/parent reuse Type flags/target/aux before target IDs are
  assigned. Cross-domain dependency traversal is one iterative dispatcher, not
  mutually recursive C functions. V covers aggregate projection/materialization
  and reflection traversal; scalar-only flat layouts need no deep V stack.

ABI4 derives only physical source-backed auxiliary slices: source token T+1;
physical expressions E+1 (the original first-constant exemption); ConstantDecl
E+1; F=floor(T/6); P=floor(T/4); syntax arguments E; generic syntax TS=floor(T/2)
(each reserved generic prefix owns name and '<', even malformed prefixes);
local/source-scope S+1; generated Function temporaries at most X; semantic map
I+P+1; reflection type map Q+7. The named public Q/M/R/X/K/L/I/O/D/V/W limits
control actual appends. Scalar values do not need aggregate pool records.

### Exact slice formula and phase overlap

For each slice apply checked align_up(cursor,alignof(record)), then checked
count*sizeof(record), then add. This ordered layout is the size query, not a
serialized ABI. All counts/products/IDs are validated before binding storage.

Persistent, in order: Core; Expr[physicalE]; Stmt[S]; ConstantDecl[E+1];
Function[F]; Formal[P]; Argument[E]; callee_map[physicalE]; GenericSyntax[TS];
Type[Q]; Member[M]; Resource[R]; Constant[K]; ConstValue[L]; Access[X].

Phase union starts at the maximum alignment of its alternatives:
A. Token[T+1].
B. Ir[I]; Local[S+1+X]; Binding[S+1]; u32 semantic_map[I+P+1];
u32 typed_operands[O]; u32 reflection_map[Q+7]; TargetDecl[D]; Walk[V]; u32 words[W].
The union consumes max(bytesA,bytesB), rounded to its maximum alignment.

Tokens die after parse. Syntax/member source spans persist through validation;
all types/constants/layout/IR/access/target metadata and staged words remain alive
through output admission/publication. No overlapping scratch slice is reused
while any resolver, graph validation or reflection walk can still read it.
Scope dies after sema, but this conservative profile does not rely on reusing it.
Target operands stream from existing typed operand/member/constant spans into W;
there is NO separate target-operands array or public pool in revised ABI4.
Every variable instruction checks `1+operand_count<=65535` before streaming.

Probe result: persistent86272 bytes, token alternative24588, lowered53784;
Fixed total140056/alignment8. Probe's actual address-escaping stack frame140080.
Proposed reviewed ceilings: fixed workspace147456 bytes (144KiB), ABI1 wrapper
147968 bytes (workspace ceiling+512). This is explicit growth from accepted M9
119408/119520. No implicit malloc/OOM contract and no additional large workspace.
All other compiler frames retain<=8192. Actual implementation must reproduce
record widths and fit the ceiling, or obtain a pre-implementation amendment.

The probe cannot measure future recursive code. Proposed implementation budgets,
not measurements: non-wrapper acyclic frame sum<=16384; combined parser
expression/type-syntax SCC sum<=512 and parser-control SCC<=256; semantic
expression SCC sum<=1536 and semantic-control SCC<=768; structured-emitter
SCC sum<=384. Active expression/type counters cover every mutual parser
transition, including extent and attribute expressions, without restarting at
nested parser entrypoints. Fixed Dtype=18; old job Dtype=64; ABI4 uses its
explicit Dtype<=64. The parser phase is bounded by
512*(Dexpr+Dtype+2)+256*(3C+4):23552 fixed and92160 maximum-job.
The semantic phase retains1536*(Dexpr+2)+768*(3C+4):43008 fixed and178176
maximum-job; emitter phase retains384*(2C+Dexpr+4). Combine phase contributions
by max. The corrected parser row is smaller than the semantic bound, so no
combined ceiling increases. Dependency/layout/materialization graph walks stay
iterative; their declaration-chain lengths add no C recursion multiplier.
Type/dependency/materialization graph traversal is iterative and adds no type
chain multiplier. This yields fixed combined library bound207360 with ABI1
ceiling and maximum-job bound195584 with caller-workspace wrapper<=1024.
Propose rounded combined ceilings212992 and196608 respectively; verify from the
actual future source/call graph, not from this data-layout probe. Excludes libc,
OS, caller and sanitizer frames. No observed-stack-peak claim.

## Semantic IR and operand charging

ABI4 I/O are explicit. Actual IR append checks, not a source formula, determine
capacity failure. Preserve old scalar appends exactly. New source constructors,
vector operators/conversions, value extraction and reference load/store each
append at most one typed semantic IR operation after evaluating operands.
The operation carries shape/path metadata; lane safety and target spilling are
TARGET expansion, not duplicated semantic expressions.

Each emitted explicit call/constructor operand owns one charged source child
root; append once to O. A vector source operand remains one value operand, not
four hidden IR operands. Composite constant children belong to L, not O.
Implicit materialization is folded into the existing typed operation or produces
one checked IR conversion for an actual runtime value, charged to I; no subtree
replay. An access step is appended only on the emitting visit, never validation-
only traversal. Each root-to-leaf access range is source-owned; compound uses may
reuse it at most twice (load then store). IR values need at most I identities;
formal values occupy I+1..I+P; slot0 stays sentinel. Generated local/temp slots
are<=S+1+X, and the target map is never indexed by target IDs.

## Target descriptors and exact operation rows

Every descriptor emits at most one result ID; no descriptor per target ID or
per source scalar annotation. Functions/signatures remain in Function/Formal
records; scalar IR constants remain in IR, with their target ID in semantic_map.
The fixed primitive/guard declaration owner retains bounded static slots.
The compatibility/profile bound D=6Q+2R+K+L+M covers: Q aggregate types; <=3Q pointer
variants for Function/Uniform/StorageBuffer; <=Q array-length constants; <=Q
implicit typed zeros; <=2R root wrapper type/pointer pairs; <=K aggregate
constants; <=L inline leaf constants; <=M member-index constants. Interning only
reduces these counts. Input pointers and scalar guard types/constants fit the
retained static allowance256. Vector safety is scalarized by lane to avoid an
unaccounted family of vector guard/splat constants. Type masks, if used for
vector comparison results, are committed logical types and charge Q.

Rows below count SPIR-V words INCLUDING opcode and ALL new result/label IDs.
Count actual operands first and check16-bit instruction width. Names use
s=ceil((UTF8 name length+1)/4). n is actual path indices including a synthetic
root index; a is actual call/constructor arity; p is formal count; w<=4 lanes.

| Target operation | IDs | Words |
|---|---:|---:|
| TypeVoid/TypeBool |1|2|
| TypeInt / TypeFloat |1|4 /3|
| TypeVector / TypeArray / TypeRuntimeArray |1|4 /4 /3|
| TypeStruct(m members), its Offset decorations |1|2+m+5m|
| TypePointer |1|4|
| ArrayStride decoration / Block decoration |0|4 /3|
| TypeFunction(p) |1|3+p|
| Scalar bool / other scalar constant |1|3 /4|
| ConstantNull / ConstantComposite(a) |1|3 /3+a|
| OpName |0|2+s|
| Variable / Load / Store |1 /1 /0|4 /4 /3|
| Function header + entry label + end |2|8|
| FunctionParameter / FunctionCall(a) |1 /1|3 /4+a|
| Return / ReturnValue / Unreachable |0|1 /2 /1|
| Unary, bitcast, simple conversion |1|4|
| Native binary/compare |1|5|
| Select / Phi(two incoming) |1|6 /7|
| CompositeConstruct(a) |1|3+a|
| CompositeExtract(d) / Insert(d) |1|4+d /5+d|
| VectorShuffle(w) |1|5+w|
| VectorExtractDynamic / InsertDynamic |1|5 /6|
| AccessChain(n) |1|4+n|
| ArrayLength(root,direct member) |1|5|
| SelectionMerge / LoopMerge |0|3 /4|
| Branch / BranchConditional / Label |0 /0 /1|2 /4 /2|

TypeArray is four words: opcode,id,element,length-ID. Decorated arrays add4.
Struct member-offset literals are checked u32 before any target planning success.
Every named struct/resource/type dependency is validated for target format even
if unused and every output mode uses the same admitted declaration set.

Derived retained scalar sequences:
- Unsigned safe div/rem: one zero comparison, safe-denominator select, division,
  final select =>4 IDs/22 words. Signed adds4 comparison/logical operations =>
  8 IDs/42 words. Float div/rem =>1 ID/5 words.
- Float-to-integer safe conversion: IsNan, select, lower compare/select, upper
  compare/select, convert =>7 IDs/36 words. Numeric-to-bool =>1/5;
  bool-to-numeric=>1/6; other conversion=>1/4.
- Masked shift=>2 IDs/10 words. Min/max=>2/11; clamp=>4/22;
  scalar exp/tanh/sqrt=>1/6 (one shared import declaration separately).
- Scalarized two-vector safe operation: each lane costs two extracts (2 IDs,
  10 words) plus that scalar sequence, then one vector construct (1 ID,3+w).
  Worst signed div/rem at w4 is41 IDs/215 words. Float conversion at w4 needs
  one extract per lane and is33 IDs/171 words. Mixed scalar/vector operands
  reuse the scalar value, never evaluate it w times. Ordinary mixed arithmetic
  may add one splat construct (1 ID,3+w) before the native operation.
- Plain if scaffolding:3 labels, merge/conditional, two live branch-to-merge,
  labels =>3 IDs/17 words; both-terminated alternative adds at most1 Unreachable.
  Loop scaffolding <=5 IDs/29 words outside its already-counted body/test/tail.
  Short-circuit scaffolding outside RHS=>3 IDs/20 words (two labels plus Phi).

Robust access: evaluate source base/indices beforehand. For q indexed steps,
unsigned check costs1 ID/5 words; signed nonnegative+bitcast+upper+and costs
4 IDs/19 words. Conjoining q predicates costs max(q-1,0) IDs and5 each.
A runtime tail adds1 ID/5 words from the correct root ArrayLength. A valid-path
load then adds5 IDs/(28+n) words for two labels, merge/branch, AccessChain,
Load and Phi; store adds3 IDs/(20+n). Pointer formation is in the valid branch.
A checked vector value extraction uses4 IDs/25 words after the bound predicate.
An array value spill adds one Function variable (1/4) and source-point store(0/3)
then the same guarded access. Spills initialize on each dynamic execution.
The path never forms an invalid subobject pointer; OOB read is a typed zero,
OOB store is discarded after required source evaluation. Per-step bounds remain
independent even if predicates for ONE reference are conjoined. Unrelated source
loads have their own guards and may return a valid zero index for an outer load.

### Whole-module conservative envelopes

For configured capacities, these conservative estimates account for the listed
rows. They are reviewable implementation budgets, not a completeness theorem
requiring every new source to fit. ABI4 uses explicit D and other append-checked
pools; exact successful planning/emission establishes actual counts:

`target_ID_bound <= 256 + D + 64I + 3F + P + (S+1+X) + 10X + R`

`words <= 2048 + 256I + 32F + 4P + O + 9D + 6M + L + 4K + 57X + 40R`

Explanation:64/256 cover every constant/native/vector safety/body/control IR row;
10X/50X cover at most two guard walks per owned step; n indices are included in
50X; extra7X words covers spill variable/store; source+spill local IDs are separate.
D covers actual type/pointer/constant descriptors;6M covers variable struct member
operands and offsets; L/4K cover composite child/vector-repeat operands; formal/
argument variable operands contribute4P/O; functions<=32F; resource variable,
coordinates/access decoration and max63-byte OpName<=40R. The2048 base covers
header, fixed primitive/guard/input declarations, entry/interface/import metadata.
This is a safety envelope, not minimum requested W or allocation by array length.

Use checked uint64 for these estimates; clamp the internal target reservation
ceiling to UINT32_MAX-1 when a conservative bound exceeds the target format,
then check every actual reservation. Do not reject a previously valid old-job
capacity vector merely because a new conservative multiplier would overflow:
retain old query admission checks, not a stricter aggregate bound at query time.
ABI4 counts themselves must fit their concrete ID/slice domains. The target
header bound is actual highest ID+1, never a saturated/conservative estimate.
Staging W remains caller-selected and actual writes are independently checked.
Same W and all other scratch limits apply to MEASURE and every output selection.

## CLI profile and unselected output surface

Keep source65536,T8192,S1024,E4096,W65536,depth32/16. Binary-only ABI4 profile:
Q256,M1024,R32,X5121,K1024,L4096,I24577,O4096,D7744,V1281,type-syntax-depth32;
TS derives T/2=4096. These are explicit reviewed caller budgets, not claims that
all valid source below65536 fits. No automatic retry/growth after a failing test.
Existing --reflection and --reflection-v2 keep old schemas and adapter profiles.
No --reflection-v4 JSON option is selected for M10; typed native reflection is
the new projection. This removes an unnecessary unclosed JSON schema surface.
No-reflection binary consumer keeps unchanged --shader-path/--out syntax.

These records/rows/profiles and ceilings are now frozen. Candidate implementation
and independent acceptance must verify actual sizes/frames/rows and historical
success, without changing this ledger retroactively to excuse a failure.
