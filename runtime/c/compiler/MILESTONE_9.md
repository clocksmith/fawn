# C compiler milestone 9: scalar functions and typed builtin calls

Frozen after lead and independent review, 2026-10-23. Not implemented or accepted.
Implementation and test changes still require the lead's explicit authorization
after independently established evaluation obligations. Baseline: accepted M8
`4e03682849db8101cc8ed35a8243433c305852d5`.

## Responsibility and retained boundaries

Own module-scope scalar function declarations, signatures, calls and returns,
and one typed scalar builtin-call owner for min/max/clamp/exp/tanh/sqrt. Extend
the shared C parser, semantics, typed IR and SPIR-V emitter, never a shader-name
recognizer, host interpreter of runtime functions, or second fixed/job grammar.
The unchanged consumer remains compile_job -> C CLI ->
`bench/gates/spirv_val_gate.py`. Production Zig selection and its public owners
remain untouched. Experimental capacities are checked safety envelopes, not
permanent language dialects or the final architecture of the full C migration.

Normative sources are the [WGSL CRD of 21 October 2026](https://www.w3.org/TR/2026/CRD-WGSL-20261021/)
(scope, stage, overloads, behavior, function calls and selected math), the official
[SPIR-V specification](https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html)
and [GLSL.std.450 revision 17](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html).
Retained Zig frontend/signature/IR/function-emission seams are useful ownership
references, not semantic or numerical oracles. In particular, do not copy loose
builtin arity, spelling-based call resolution or absent cycle validation.

Keep exactly one compute entry, current input/resource/layout/reflection limits,
u32/f32 storage and flat uniforms, existing language/control depth envelopes,
SPIR-V 1.3 and explicit Vulkan 1.1 validation. Multiple-entry selection, arbitrary
aggregate values/signatures, pointers, private/workgroup memory, collectives,
textures, f16, other math, overrides, aliases, phony assignments, user const
functions and other targets remain separate owners. None of these exclusions
authorizes weakening their future migration obligations.

All three public headers, five exported functions, enum values, exact record
layouts, CLI options/capacities and reflection JSON schemas remain unchanged.
The private workspace requirements increase as described below; callers must
query the loaded library, as their existing API contract already requires.

## Function language and meaning

Admit any number of helpers that fit actual storage. A helper has zero or more
immutable by-value bool/i32/u32/f32 formals, and either one such result or no
result. Omitted return type means no result, not a fifth scalar type or a
spelled `void` type. Trailing parameter/argument commas follow admitted WGSL
grammar. There are no nested functions, prototypes, overloading or first-class
function values. Entry parameters remain separate builtin inputs, not helper
formals, and the existing five-input envelope does not cap helper arity.

Parse every declaration before semantic resolution. Module names are hoisted;
signatures are available independently of source order. Reject duplicate module
names, duplicate formals and illegal helper/parameter/result attributes. Scalar
annotations use module lookup; formal names enter scope only in the body, not
other signature annotations. In a body resolve nearest lexical bindings, then
its own formals, then module declarations, then predeclared names. Root-body
locals cannot redeclare a formal; nested scopes can shadow it. A shadowing local
does not become callable merely because a module function or builtin has the
same spelling. User functions may shadow builtin spellings and then receive
ordinary user-function semantics. Types and constructors obey the same lookup.

Helpers use all admitted scalar statements, loops, short circuit and resource
operations. They may read declared uniform/storage globals and write permitted
storage globals. They do not capture an entry's locals, alias or builtin inputs,
nor another helper's formals/locals. Scope state, loop targets and behavior state
are reset for each body. Formals reject assignment, compound update and ++/--.

Arguments are type-checked/materialized to their individual formal types, without
implicit concrete conversions or expected-type propagation through an abstract
subtree. Evaluate once, left-to-right, retaining value snapshots before entry
to the callee. Each invocation gets fresh function-local storage. Return resumes
the caller; it does not terminate the shader entry. Repeated/nested/loop calls
must preserve live caller locals and all argument/resource side effects. Do not
reuse mutable resource loads across calls; conservatively treat every user call
as potentially writing storage. No call purity optimization is required.

User calls are runtime-stage even for literal arguments. They cannot initialize
module/local consts or constant attributes. A no-result call is permitted as a
call statement, never as a scalar expression/argument/initializer/return value.
An ordinary returned-value helper may be called as a statement and discard its
value. Admit `@must_use` only on a helper with a result; reject its whole-call
statement use. All selected numeric builtins and existing constructors/bitcast
are must-use. A local initialization can explicitly consume an unwanted result;
phony assignment is not added here. Call statements are admitted at the ordinary
statement and for-initializer/update positions allowed by WGSL, not general
expression statements. Calls to the compute entry reject.

`return;` is valid only in a no-result function. `return expr;` requires a scalar
result and exact matching type after permitted materialization. Validate every
return, including dead returns. A returned-value body must have exactly Return
behavior; a no-result body has a nonempty subset of Next/Return. Retain M7 loop,
continuing, bypassed-declaration and empty-behavior rejection, not a last-token
return heuristic. Dead statements still undergo name/type/stage/behavior checks.

## Dependencies and typed body representation

One function record owns one syntax statement range, expression range, typed IR
range and mutable-local range. No source-count-dependent inlining and no host
execution of runtime helper source. Reserve all target function identities before
emitting bodies; emit every validated function once, including unused helpers.
Each target signature/parameter/call/result/return is explicitly typed, using
OpTypeFunction, OpFunction, OpFunctionParameter, OpFunctionCall, OpReturnValue
or OpReturn as appropriate. Even no-result OpFunctionCall needs a target result
identity; that identity is not a usable WGSL scalar value.

Signature validation precedes body validation. Body validation resolves each
user-call expression into a checked per-expression callee map even when runtime
emission is disabled by short circuit. Repeated semantic visits are idempotent;
they do not append duplicate dependency storage. Iterative white/gray/black DFS
scans each function's expression range and this map, using checked parent/cursor
fields in function records. It checks all functions, not only entry-reachable
ones. Direct, mutual, unused, after-return and short-circuited recursion reject.
A call graph taken only from live emitted IR would be insufficient.

Retain the iterative all-initializer constant dependency owner, including skipped
references. In this scalar envelope, cross-kind cycles cannot become valid:
constant/attribute references to user calls or runtime resources fail stage
validation; type annotations resolving to values/functions fail type validation;
flat scalar member types cannot recursively name structs. These checks apply to
unused declarations. Thus constant edges and function-call edges are the only
remaining potentially valid cyclic edges, and both are traversed iteratively.
No graph walk or compilation recursively visits a callee body on the C stack.

Semantic IDs and target IDs remain separate. Allocate formal semantic IDs in a
checked range beyond instruction-result IDs, map them to parameter target IDs,
and never index a semantic map with a target ID. Track the entry's legacy alias
ID explicitly; it is not universally instruction 1 when helpers precede it.
Mutable local indices/ranges are function-owned even if global storage slices
hold all records. Target Function variables belong in each function's entry
block and receive source-point initialization on every executed iteration/call.

Reflection stays declaration-based, retaining every declared resource/struct in
source order, including resources used only by unused helpers. It is not an
entry-reachable-resource query and does not migrate Vulkan TranslationInfo or
its clone/free/error/cache contracts. Only the single entry's declared inputs
enter its interface; helper parameters do not become interface globals.

## Selected scalar builtin owner

Use one closed typed builtin identity/signature table shared by resolution,
constant evaluation and IR lowering. Exact arities are two for min/max, three
for clamp and one for exp/tanh/sqrt. min/max/clamp admit AbstractInt,
AbstractFloat, i32, u32 and f32; exp/tanh/sqrt admit AbstractFloat and f32, with
feasible abstract conversion/ranking. bool and mixed concrete scalar types
reject. Runtime participation excludes abstract overloads. Constructor/bitcast
semantics and their distinct conversion ranks remain those of M8.

For integer min/max/clamp use exact signed/unsigned ordering. For floats choose
ordered comparison/select min/max with the right operand on equality, retaining
the M8 max choice. Choose clamp as min(max(value, low), high), for all types.
Reject reversed const-expression bounds even if value is runtime or the call
appears in a numerically skipped operand. Do not treat literal-initialized let
reads as const bounds. Runtime reversed bounds are valid and yield the chosen
composition, never GLSL.std.450 FClamp/UClamp/SClamp poison. Emit safe min/max
composition, not a select over an already invalid Clamp instruction.

For runtime f32 exp/tanh/sqrt emit their typed GLSL.std.450 operations. No host
math is used for runtime expressions. Domain/nonfinite runtime inputs follow
the dated language guarantees and chosen target operations, not a new promise
of portable GPU payload identity. The instruction model's deterministic choices
are distinct from physical-device accuracy. Preserve M8 arithmetic boundaries,
no reassociation/contraction and per-resource robust accesses around calls.

Constant math is fully admitted, not a runtime-only spelling gate. The selected
compiler-host implementation is the existing Linux x86-64/GCC14.2 IEEE host with
glibc2.41 libm, strict FP flags, nearest-even and gradual underflow. Use expf,
tanhf, sqrtf for f32 and exp, tanh, sqrt for AbstractFloat; force the declared
result width before further evaluation. Do not claim exp/tanh are correctly
rounded. The chosen host acceptance policy is at most 2 ULP error versus an
independently enclosed ideal result for finite exp/tanh outputs, and nearest-even
sqrt; this is an implementation-quality policy, not an interpretation that WGSL
requires binary64-correct transcendental functions. Pin actual host/library
identity in receipts and check repeatability separately from accuracy intervals.

Reject evaluated constant sqrt of a negative nonzero input and every evaluated
nonfinite constant intermediate/result. Accept zero, signed zero, subnormal
and finite underflow results according to the selected host policy; errno ERANGE
alone is not a shader error. exp overflow must fail atomically. Skipped numeric
evaluation retains M8 rules; type/name/dependency and mandatory clamp-bound
checks are not skipped. Independent acceptance distinguishes const errors,
runtime stages, permitted numerical intervals and the chosen deterministic host
results. It must not compare two calls through the same host libm and call that
an independent oracle.

Preserve all caller FP state, including rounding, flags, enabled traps and SSE
FTZ/DAZ. Also preserve caller errno on every public API exit, including preflight,
save/normalize/restore failures and success. Save it before any potentially
mutating host operation and restore after cleanup. Complete every fallible FP
restore before word publication. Retain explicit restoration-failure status and
bounded saved-state recovery without claiming restored state when not proven.
No process-global environment changes, hidden allocation or cross-thread state.

## Caller storage and private record plan

Let T/E/S/W be configured tokens/charged expressions/statements/staging words.
Keep old budgets and accounting exemptions unchanged. Required function braces
and signatures do not consume S; a call expression consumes one E plus operand
nodes, a call statement consumes one S, and return-with-value consumes one S
plus its expression nodes. New syntax never steals the old leading-constant,
leading-entry-alias, outer-if-comparison or bare-attribute exemptions.

- F=floor(T/6) function records, including the entry. A complete function requires
  at least fn/name/( )/{ }, six distinct tokens, even with no formals/body nodes.
  Check capacity before every record access; malformed prefixes need not receive
  storage for a complete function. A zero-F configuration fails without access.
- P=floor(T/4) helper formal records. Each formal consumes name/colon/type plus
  its preceding opening parenthesis or comma; those four tokens are disjoint
  across formals. Commit a formal only after parsing its type. Entry builtins
  retain their separate fixed records. No arbitrary 16-formal limit is added.
- A=E syntax argument links and E typed argument values. Every argument has a
  distinct charged root node; none of the historical exempt nodes is a call
  operand. Include constructor/bitcast/builtin argument accounting if those
  use this common storage. A nested argument root belongs to exactly one list.
- R=physical expression capacity resolved-callee slots: E+1 for jobs and
  E+S+1 for fixed adapters. Each call contributes at most one graph edge, so
  D<=E. The map uses R rather than assuming charged IDs are physical indices.
  Constant dependency scan/parent state remains inside its C=E+1 records.
- Keep I=1+4E+8S job instructions, fixed I=385, and S+1 mutable-local and scope
  records. Parameters live in P records and do not inflate/crowd out S storage.
  V=I+P+1 semantic-map entries cover instruction and formal identities.

Private declaration plan (not a new public header): a function record is at
most80 bytes/alignment4: name and result-annotation spans; start/count or start/end
pairs for formals, statements, expressions, IR and mutable locals; result type,
flags/behavior and formal-value base; and a12-byte union of DFS state
(scan,parent,color) with target state (function,type,entry-label IDs). A formal
record is16 bytes/alignment4: name span plus a union of annotation span with
resolved type/source offset. Annotation bytes are reused only after every
signature has been validated. Syntax argument links are8 bytes/alignment4
(expression,next); typed arguments and callee-map elements are uint32_t.

Reserve a call's complete typed-argument segment only on its emission path,
at most once per source-call lowering, before evaluating its arguments, then fill
in order. Validation-only or repeated non-emitting visits reserve no typed
arguments. Nested calls reserve disjoint later segments. Serialize from
these spans directly, not a formal-count-sized stack array. Function signatures
likewise stream checked formal types directly to the private word staging buffer.
Token storage dies before the existing token/lowered union is reused. Function,
formal, syntax-argument and callee-map records remain live through semantics;
typed arguments and expanded value maps join the lowered phase. Reuse DFS bytes
as target IDs only after every cycle check completes. No library allocation,
VLA, extra hidden workspace or input-sized automatic arrays.

Every product, sum, alignment, range endpoint, append and cast is checked before
use. Zero E/S permits empty helpers and arbitrarily many formals within T, while
zero-argument calls still consume E/S as stated. Layout query overflow leaves
requirements untouched. Counts above UINT32/source-span limits reject explicitly.
Actual instruction/value/formal/argument/graph accesses check their own bounds.
Source length or extra physical bytes cannot override configured budgets.

### Explicitly reviewed fixed-memory boundary change

M8's76512-byte measured workspace cannot honestly absorb new token-derived
metadata under its77760-byte ceiling. The reviewed new ceiling is122880 bytes
(120KiB) and ABI1 GCC14.2 -O2 frame<=123136 bytes; every other library frame stays
<=8192. Preserve the old source/token/E/S/depth/I/word limits, not the old private
memory ceiling. ABI1 remains a measured transitional wrapper; caller-workspace
APIs are the scalable path, not a permanent mandate that all Doe use this stack.

For fixed T2048/E320/S16, F341/P512/R337. The conservative incremental ledger is
80F+16P+8E+4E+4P+4R+512=43220 bytes, allowing up to512 bytes of core pointers,
counts, alignment and bookkeeping growth. Added to76512 this is119732 bytes,
below122880. This is a record-design bound, NOT a measured candidate sizeof.
The lead explicitly approved this changed boundary before freeze. Acceptance requires actual
record, whole-workspace, ABI1-frame and other-frame measurements. Do not raise
the ceiling later merely to hide an implementation overshoot. Expression/control
recursion remains18/4 fixed and64/32 job, with a new combined-stack calculation;
function/call-graph count adds no C recursion depth.

## Complete semantic/target/output ledger

Function headers, parameters and implicit no-result termination are metadata,
not fictitious E/S nodes or IR instructions. A user/builtin call adds at most
one typed instruction; each argument's materialization belongs to its charged
expression, including nested calls. A typed return adds one statement instruction.
Clamp is one IR operation expanded by the backend. Thus the retained four-IR-per-E
and eight-IR-per-S proof remains sufficient; formal values are the explicit P
addition to the semantic map, not uncharged instruction results.

Target reservation is B=256+32I+3F+P, checked before narrowing and including the
final header bound. Its fixed declaration term covers existing resources,
structs, inputs, scalar/pointer types, constants and one GLSL import. Each
function adds at most one identity, one function-type identity and one entry
label; every formal adds one identity. Locals remain covered by charged statement
IR. Every actual reservation still checks, including no-result call result IDs.
The fixed case B=14111 fits. Unrepresentable configured derived bounds are
INPUT_ERROR, not truncated IDs or a larger hidden map.

Per-new-operation target maxima are1 ID for user calls (including discarded or
void),1 for scalar exp/tanh/sqrt,2 for comparison/select min/max,4 for clamp,
and0 for return/return-value terminators. Existing guarded operations retain the
M8<=32-ID ledger. No function-body or target-ID-indexed scratch array is needed.

A conservative whole-module word ledger is2048+256I+32F+4P+A. Fixed declarations,
maximum-length resource/input/entry names and the GLSL import fit2048; each old
IR expansion, including its local declaration/constant, fits256. Each function
has at most18 debug-name words,3 fixed type words,5 function words,2 label words,
2 terminal words and1 end word, fitting32. Each formal adds one signature word
and three parameter words; do not emit formal-parameter OpName records in this
owner (their names remain available for source diagnostics). Calls add four fixed words under the IR term plus
one word per argument under A. min/max/clamp need at most11/11/22 words;
scalar math needs6; return-value needs2. This is a linear upper ledger, not an
allocation requirement or replacement for independently configured W. Every
actual write checks W and caller capacity; no partial caller publication.

SPIR-V instruction word counts have a separate16-bit format bound. Check
3+parameter_count for OpTypeFunction and4+argument_count for OpFunctionCall
before serializing anything. A too-large instruction is UNSUPPORTED with a
named SPIR-V instruction-word-count diagnostic at the relevant declaration/call,
not truncation, a VLA, or a guessed small grammar cap. In particular65532 formal
types can fit a type instruction, but65532 arguments cannot fit a call. Exercise
both actual boundary cases through the direct job API with sufficient source,
token, expression, statement and word budgets, not the smaller CLI envelope.
Distinguish an earlier legitimate budget error from reaching this format check.
No output-side
guard is waived because token storage or target IDs happen to fit.

## Preservation and independent acceptance

Retain ABI/null/exact-size/alignment/disjointness checks and statuses. Failures
zero count/reflection and preserve all caller words; all fallible work precedes
publication. Reuse after any result needs no reset; independent workspaces can
run concurrently. Preserve no-allocation library behavior, CLI acquisition and
no-clobber/rollback boundaries, source offsets and all historical seals/receipts.
Equivalent admitted syntax has identical current words across adapters where
reflection permits; historical target-ID/binary identity is not promised.

Before implementation freeze independently specified cases and oracles for:

- Every helper scalar signature/result/no-result case; zero and >16 formals;
  forward chains/diamonds/unused helpers; side effects, repeated calls and fresh
  locals; early/nested/loop returns, bool returns and complete behavior negatives.
- Name/type/formal shadowing, cross-scope noncapture, wrong arity/materialization,
  immutable formals, entry calls, must-use/discarded values, const-stage user-call
  rejection and recursion in live/dead/unused/short-circuited references.
- Each builtin arity/type/stage row, abstract ranking, constant bound errors next
  to runtime values, valid reversed-runtime clamp, signed/unsigned boundaries,
  zeros/subnormals/extremes/domain/overflow and independent numerical enclosures.
- Actual instruction execution of function/parameter/call/return frames and
  ExtInsts, parameter/result typing, caller continuations, one-time ordered
  argument snapshots, robust accesses and post-side-effect resource loads.
  Include valid emitted mutants, not only validator rejection or opcode text.
- Empty-helper/formal pressure at zero E/S, token-derived metadata limits,
  physical-versus-charged graph indices, long iterative graphs, exact argument
  and instruction-format edges, checked size/alignment/ID/word overflow, redzones,
  short buffers, failure/success/reuse/concurrency and all API/CLI paths.
- C/C++ archive/shared/prebuilt consumers, allocator exclusion, ASan/UBSan,
  actual private/frame/combined-stack measures, all caller FP/errno states and
  injected host failures before publication. Freeze independent high-precision
  references without candidate inspection or a shared-libm self-comparison.
- Every immutable M1-M8 suite and accepted input, each intentionally lifted
  rejection named explicitly; full98-path consumer discovery with separate
  emission/validation/instruction/physical/adoption stages.

Ten unchanged potential-positive witnesses are stable_fluids_copy_scalar,
moderate_gelu, gelu, gelu_gated, attention_slice_forward_f32,
attention_slice_pairwise_f32, both attention_slice v2 variants, simple_silu and
rmsnorm_serial_f32. All113 trace-file hashes and13 witness hashes matched the
supplied source-bound trace while HEAD was the accepted M8 baseline; all ten positive sources were
read in full. Their source-bound dependencies remain in the next-owner evidence.
They are NOT passing observations. Independent execution uses separately packed,
independently sized resources and numerical requirements for each unchanged
source, not mandatory equality between different attention summation orders.

complex_multi_entry additionally requires selected-entry policy;
stable_fluids_pressure requires aggregates; shader_compile_pipeline_stress requires
other math/phony disposition. None is a positive witness here. Retain every
production/public compiler owner: source trace coverage of132 facade exports and
57 direct consumer references is not complete transitive adoption evidence.

No CPU instruction oracle, target validator or host math test establishes
physical Vulkan/Metal/D3D12 accuracy, runtime adoption, compilation/runtime
performance parity, browser ownership or completion of the full Doe migration.
