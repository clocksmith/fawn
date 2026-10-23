# M12 unchanged-storage and exact lowering ledger

Reviewed contract ledger, not a new layout/stack measurement or implementation
acceptance. Companion MILESTONE_12.md applies. Accepted M11 baseline is
`f01effdf070edf2f25cf0f2016c995df1e4d3bab`; all 30 compiler dependency files match.

## No new caller storage or ABI

Preserve M11 record widths/alignment: Core552/8, Type56/8, Constant32/8,
ConstantValue16/8, IR32/4, TargetDeclaration32/8. No persistent pointer, count,
cache, mapping, target operand arena or public capacity is added. Builtin registry
is bounded read-only static metadata; semantic signatures are not per-call pool
records. A proposed entry {name pointer, kind, arity, family, target selector}
is 24 bytes on the qualified x86-64 ABI; eleven entries cost264 read-only bytes
plus names, outside caller workspace. Verify layout later rather than assuming
it from this calculation.

Current M11 layout evidence, not newly measured here:
- Fixed146256/alignment8; persistent92456, common20856, target32944,
  uniformity5232, token24588; target remains largest scratch alternative.
- Binary CLI2536128; old-job CLI-capacity2172992.
- No proposed delta in any layout query. Fixed146256 leaves1200 bytes beneath
 147456 but that spare space is not permission to add records or stack locals.

Unchanged profiles: fixed T2048/E320/physicalE337/S16/F341/P512;
Q32/TS32/M64/R8/X337/K64/L128/I385/O320/D464/V97/W4096.
Old job uses its existing formulas I=1+4E+8S, physicalE=E+1,
X=E+S+1,O=E,F=floor(T/6),P=floor(T/4),G=floor(T/8), with compact aggregate pools.
ABI4 keeps explicit pools. Binary CLI stays T8192/E4096/S1024/Q256/M1024/R32/
X5121/K1024/L4096/I24577/O4096/D7744/V1281/W65536 and existing depths32/16/32.
No capacity auto-retry, no new profile for one builtin or witness.

M11's exact ordered slice/phase-union formula is inherited without modification.
Syntax, constant/type/IR and source maps persist; uniformity scratch expires
before target declarations/words; no overlap is reused early. Semantic value map
remains I+P+1 (fixed898), never indexed with generated target IDs.

## Semantic and constant charges

One runtime builtin costs one actual IR append after argument operations, and
exactly its arity O entries. IR retains the three argument semantic IDs and kind
in the four-word payload; operand-count validation and O charging remain real
even if direct fixed-arity payload fields are used by the emitter. Semantic
return TypeId can differ from operand TypeId, which is recovered through the
typed semantic map/IR or formal owner, not assumed equal. Do not consume extra
semantic IR/value-map slots for target lane results.

Input argument constructors/conversions/accesses retain their existing E/I/O/X
charges. One selected abstract/common vector type charges Q once if not already
interned; shape-reducing result is a primitive and needs no Q record. No implicit
bool-vector masks or vector guard/splat types. K/L charges for an evaluated vector
result are one COMPOSITE node plus N edges; scalar result zero K/L. Input bound
materializations and constructors charge existing K/L; copies share immutable
nodes. Committed conversions keep source-node/destination-type cache identity.
Neither a failed candidate overload nor lane projection may create a hidden
constant tree. Mandatory skipped clamp checks use real const pools, and vector
results never allocate temporary arrays proportional to user array extents.

All N<=4 numerical scratch is automatic, nonrecursive work in a leaf helper.
The semantic call handler holds at most three ExpressionValues and three syntax
IDs. It must not add a [3][4] NumericValue array to each recursive sema frame.
Project a lane into at most three scalar values, accumulate/reduce sequentially,
and finish a result in ConstantValue[4] (64 bytes). These small arrays belong to
nonrecursive helpers where possible; they still count on the actual acyclic
call path. No VLA, alloca, hidden static mutable scratch or caller-span borrow
after return.

## Exact AI accumulator for the disclosed N1 interpretation

Four AI operands products have absolute value at most2^126 each, and four same-
sign products sum to2^128. A signed128 accumulator is INSUFFICIENT. Use two
five-u32-limb nonnegative sums (positive and negative, 160 bits each), one four-
limb product, two two-limb magnitudes, bounded carry temporaries. Form |INT64_MIN|
in unsigned arithmetic; never negate it as signed C. Multiply two32-bit limbs
with uint64_t, and add bounded partial limb+carry values with an explicit proof
that their maximum is2^64-1. Add each four-limb product into the appropriate
five-limb sum. Subtract the smaller sum from the larger once, check signed64
final range, and encode only after the check. No __int128 requirement or malloc.
Five limbs also cover carry and exact cancellation near ±2^128. This selected
primitive needs no persistent record/pool. Its mathematical bound and N1 disclosure have passed design review;
implementation and independent executable acceptance remain pending.
If review changes the selected interpretation, revise the exact numerical graph
and cases explicitly; do not implement different adapter dialects.

## Target rows, counting all result IDs and opcode words

N is actual width2..4. Every source argument has already been evaluated once.
Counts below include the final result ID but exclude input results/types and a
shared import. Temporary count is total IDs minus one. Every append checks W;
every ID reserve checks the target format; variable instructions retain the
16-bit word-count guard even when this family is intrinsically tiny.

| Selected operation | Construction | IDs | Words |
|---|---|---:|---:|
| scalar min/max | compare, select |2|11|
| scalar clamp | compare, select max, compare, select min |4|22|
| vector min/max | 2 extracts+compare+select per lane, construct |4N+1|22N+3|
| vector clamp | 3 extracts+2 compares+2 selects per lane, construct |7N+1|38N+3|
| scalar/vector exp/tanh/sqrt | typed OpExtInst unary |1|6|
| f32 vector dot | OpDot |1|5|
| i32/u32 vector dot | vector IMul; N extracts; N-1 IAdds |2N|10N|
| scalar/vector length | GLSL Length |1|6|
| scalar/vector distance | GLSL Distance |1|7|
| vector normalize | GLSL Normalize |1|6|
| vec3 cross | GLSL Cross |1|7|

Derivation: one Extract=1ID/5words; compare=1/5; Select=1/6;
vector Construct=1/(N+3); vector IMul=1/5; IAdd=1/5.
For integer dot do not multiply each lane again after vector multiplication and
do not seed with zero. N extracts and N-1 adds account for the final semantic
result. i32/u32 multiplication and addition preserve low32 bits; no extension,
64-bit target type, capability or target aggregate constant is needed.

Worst new row is vec4 clamp29IDs/155words. Vec4 min/max17IDs/91words;
integer vec4 dot8IDs/40words. Every row remains dominated by retained64I/256I.
Vector comparisons stay scalarized, so they do not consume Q/D boolean masks.
Typed f32 ExtInst uses its actual scalar/vector TypeId; length/distance and dot
use the scalar leaf result. Cross requiresvec3 before emission. No f16/AF/AI
target type may reach any body instruction.

Shared GLSL.std.450 import is one result ID and six words (instruction header,
result ID and four string words for the nul-terminated12-character name). It
fits the retained fixed256ID/2048word allowance. Declare it only if a resolved
emitted builtin actually needs it; module const-only use needs no import. The
registry explicitly maps Exp27,Tanh21,Sqrt31,Length66,Distance67,Cross68,
Normalize69; min/max/clamp and both dot forms map to no extended instruction.
Delete the current kind>=BUILTIN_EXP assumption: append-order is not semantics.

No body emission can add an undeclared type, pointer, constant or import. Plan
the module once from typed IR and verify target_count/constants_used stability
as before. GLSL Clamp43/44/45 must not be emitted for this family. Floating
min/max equality/NaN/zero-sign behavior keeps current compare/select policy;
out-of-domain/indeterminate latitude is not an excuse to skip argument effects.

## Whole-module bounds unchanged

Inherited conservative M11 envelopes, evaluated in checked uint64:

IDs<=256+D+64I+3F+P+(S+1+X)+10X+R+G+8.

Words<=2048+256I+32F+4P+O+9D+6M+L+4K+57X+40R+25G+64.

Here G=floor(T/8) is workgroup-root allowance. D remains an explicit append
limit; dynamic descriptor upper envelope7Q+2R+K+L+M is unchanged. No builtin
descriptor per target value. Exact reservations decide fit; the estimate does
not grant larger W or cause new query-time rejection. Saturate ID reserve ceiling
at UINT32_MAX-1 when necessary, but publish actual highest ID+1 in the header.
Checked intermediate sums/products must not wrap before saturation. MEASURE
and all output masks run the same full private emission with the same limits.

## Stack and failure budgets unchanged

Fixed workspace<=147456; ABI1 wrapper<=147968; caller-workspace wrappers<=1024;
every other individual frame<=8192; non-wrapper acyclic frame sum<=16384.
Recursive SCC ceilings: parser expression/type512, parser control256,
sema expression1536, sema control768, emitter384; uniformity expression/control256.
The builtin numeric leaf is not an additional expression-depth multiplier.
Changing sema_builtin factoring requires recalculating the complete SCC/path,
not reporting each small function independently.

Existing phase bounds remain:
parser512*(Dexpr+Dtype+2)+256*(3C+4);
sema1536*(Dexpr+2)+768*(3C+4);
emitter384*(2C+Dexpr+4);
uniformity256*(Dexpr+2)+256*(3C+4).
Maximum phase43008 fixed /178176 job, combined207360/195584 under reviewed
rounded ceilings212992/196608. Actual .su plus call-graph evidence is required
after implementation. This planning arithmetic is not an observed peak and
excludes libc/libm, OS, caller and sanitizer frames as the accepted ledger does.

Independent checks must isolate every named pool's exact/one-short boundary,
target IDs/words/16-bit width, workspace alignment/staleness, and outputs left
untouched on failure. Add a builtin at the boundary instead of raising the pool
because a chosen real source fails. Record capacity rejects honestly. No new
allocation/export/record-width claim is accepted without actual source-bound
native evidence. There were no layout probes or builds in this draft pass.
