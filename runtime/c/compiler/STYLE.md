# C compiler implementation style

Follow repository native adapter rules and the frozen milestone. This guide
records current implementation choices, not expanded qualification claims.

- Use C11, explicit boolean/status results and caller-owned spans. Check lengths,
  multiplication, alignment, capacities, IDs and publication before use.
- Keep lexical, syntax, scalar/type/constant, IR and target ownership distinct.
  One grammar/sema/emitter serves fixed and job adapters; capacity differences
  are not alternate language dialects.
- Prefer typed bounded records and named domain helpers to generic containers,
  callback frameworks, erased domain values or mechanically translated Zig.
- Preserve original source offsets. Private source spans borrow caller bytes only
  for the call; published IR/reflection names are copied. Token storage dies at
  parse return before fixed union storage becomes IR/target scratch.
- No library allocation, retained pointer, mutable global or hidden second
  workspace. CLI allocations/file descriptors have visible ownership and cleanup.
  Only owned temporary/published artifacts may be removed after failure.
- Keep original tests/seals/receipts immutable. Classify each intentional former
  rejection explicitly; do not relax whole classes to obtain a passing suite.

## Scalar and stage ownership

NumericValue carries independent IrType and bit payload. AbstractInt is signed64;
AbstractFloat is binary64. Do not use a u32 payload for abstract integers, fold
abstract floats at f32, propagate a destination type through an abstract subtree,
or treat explicit conversion as automatic materialization. Overload selection
precedes conversion, including concrete-only f32 constructor and runtime shift
rules. Let snapshots are runtime-stage reads; consts retain their numeric stage.
Override-stage values retain that stage after specialization. Validate every
module declaration and body before selecting a pipeline closure; do not replace
source text or invent values/counts to validate uninitialized declarations.
Unknown override IR and deferred array footprints are validation-only facts;
every executable target rejects them. API-provided doubles use numeric.c's
checked override conversion, never constructor saturation/truncation. Replaced
and unselected initializers are not evaluated during specialization. Keep the
original module identity separate from each independently owned specialization.
`select(false_value, true_value, mask)` evaluates all three arguments eagerly,
once and in source order. Scalar masks broadcast through a typed boolean-vector
IR splat for vector results, retaining baseline SPIR-V1.3 compatibility. Text
targets select vector components from saved values; HLSL2021 cannot use a vector
short-circuit conditional. Vector splats repeat one saved operand rather than
re-evaluating the source expression. Const selection borrows/projects the existing
constant DAG, including boolean values; it is not a second arithmetic evaluator.
Keep argument evaluation in the existing recursive call owner; a builtin's
typed finishing helper consumes snapshots without re-entering source. Measure
the complete semantic recursion group when adding a call path: individually
small frames do not establish the group's unchanged stack ceiling.

numeric.c owns locale-free bounded literal parsing, rounding, const arithmetic and
checked conversions; numeric_math.c owns declared-width component math.
Check literal overflow after rounding and significand carry: a decimal slightly
above the largest finite value may round to it. Concrete hexadecimal literals
retain their separate exact-representation requirement.
Multi-argument math uses the same typed signature and ordered operand snapshots;
preserve argument order (notably `atan2(y,x)`) in constant and target lowering.
Geometric builtins retain their operand roles: `reflect(incident, normal)` uses
the supplied normal unchanged, including non-unit or zero normals. Const-stage
geometry uses the existing typed numeric owner; target mappings stay explicit.
Ternary geometry keeps its signature-owned argument widths: refraction takes two
vectors and one scalar of their common leaf type. Preserve the third ordered IR
operand on every target. Const refraction tests its discriminant before sqrt;
total internal reflection produces zero without evaluating an invalid branch.
Face-forward uses a strict negative dot test; zero selects the negated first vector.
Validate WGSL domains rather than accepting a wider libm domain: a negative
`pow` base is invalid even when a host integer exponent would produce a finite value.
Lexer/parser preserve raw literal spans, not floating host values. Const evaluation checks every evaluated intermediate. Skipped short-circuit
numeric evaluation is distinct from required name/type/dependency validation and
mandatory divisor/count checks. Runtime operators never call the const evaluator.

The pipeline owns one NumericEnvironment through numeric_environment.c.
Save/hold/normalize caller rounding, precision, traps, exception flags and native
subnormal modes; restore before publication on both success and failure. Use the frozen strict FP build flags and qualified IEEE host,
with no contraction/reassociation or process-global locale changes. A failed
restore is a specific host error, not a claim of successful state preservation.

Typed IR uses concrete scalar and aggregate TypeIds; abstract constants stay in
the typed constant owner until materialization. Keep safe-operation guards in the
emitter: normalize integer denominators, mask shifts, and sanitize/clamp float
conversion inputs before potentially invalid target instructions. Do not select
over an already-invalid result. Signed robust indices test nonnegativity before
combining with the independent resource length check. Target bitcast semantics do
not prove universal physical NaN/subnormal payload preservation.

## Namespace and caller storage

Module declarations are hoisted and source-order independent. Constants, aliases
and struct dependencies share one iterative resolution graph. Unresolved
ConstantSyntax value storage holds scan/parent links; only a resolved constant
publishes a numeric payload into that union. `expression==NO_ID` marks an alias:
its type slot holds the parsed expression end until resolution publishes the
canonical underlying TypeId. An alias creates neither nominal identity nor a
second type registry. Validate unused aliases and all initializer references,
including skipped expressions. Constants and aliases share the existing
E-capacity+1 declaration pool (aliases need no initializer node); retain the exact
legacy constant exemption. No recursive stack proportional to module declarations
or private heap worklist is allowed.

Local scopes save/restore visible record counts. A binding enters scope after its
initializer; same-scope duplicates reject and nested/module shadowing is ordinary
WGSL lookup across all adapters. Type names, constructors, max and bitcast must
resolve normally before builtin dispatch. Immutable runtime bindings retain IDs,
not ASTs to replay; const bindings retain typed numeric values.

storage.c owns checked typed caller slices and the fixed compatibility adapters.
The [M10 ledger](MILESTONE_10_LEDGER.md) owns exact syntax/type/member/resource,
access-step, constant-DAG, IR/operand, target-declaration and traversal accounting.
Do not flatten storage proportional to array lengths, hide a second allocator,
reuse target IDs as semantic indices, or enlarge pools after a failing case.
Every append/reservation checks its domain; word capacity and target-format
capacity are distinct. Saturated format exhaustion is not a growable input error.

Fixed adapters retain exact leading-constant/global-x-alias/old-if/bare-attribute
accounting and public limits. M10 explicitly reviews workspace<=147456 and ABI1
frame<=147968, with every other library frame<=8192 and separate recursive-family
and combined ceilings. Earlier measurements remain historical, not current limits.
The parser's active expression/type counters span mutual transitions; declaration,
layout/materialization and call graphs use bounded iterative traversal.

The [M11 ledger](MILESTONE_11_LEDGER.md) extends phase accounting for uniformity
without changing public pools or those ceilings. Analyze validated source before
target emission: constant-skipped syntax still carries uniformity dependencies.
Reuse only dead phase storage; never overlay persistent typed IR or resource data.
Bound fixed-point work explicitly and fail atomically on exhaustion. Initialization
writes whole typed workgroup roots before user code; a workgroup barrier must not
become a storage-buffer fence or a cross-group ordering promise.

## Structured state and resources

Validate all source after return; only emission suppresses dead operations. Keep
actual current labels through robust and short-circuit merges. Both-return ifs
retain OpUnreachable at their structural merge; no block receives a second
terminator. Each runtime logical RHS needs a branch/Phi, not eager evaluation.

Function variables are declared at target entry but initialized at their source
point on every iteration. For-header scope survives test/update/body; body locals
cannot leak into update. Continuing may reference body scope only when targeted
continue cannot bypass its declaration. Validate behavior sets and continuing
restrictions even in dead source. A predecessorless continuing block retains only
the required structural back edge. Observation step limits are not source limits.

Resource/member identity is a typed declaration index, not spelling. Typed ABI4
resources and legacy scalar projections are explicit separate output capabilities.
One checked natural-layout result feeds validation, reflection and emission; uniform
constraints validate rather than repack. Standard-uniform layout is explicit
caller-selected language support in flags-bearing APIs; `requires` checks that
support and never enables it. Keep conservative TYPE_UNIFORM classification for
baseline physical lowering, preserve the default, and publish the selected profile.
Compact uniforms use the existing exact-byte raw uniform owner, including tail-only
blocks below 16 bytes; never infer a Vulkan feature requirement or emit zero arrays.
Minimum extents are distinct from
storage robustness. Each array access guards its own resource before even forming
an element pointer; OOB load zero can be a valid outer index. No combined-length
shortcut suppresses an otherwise valid outer operation.

SPIR-V Workgroup arrays/structs use separate undecorated target declarations,
including nested members, because baseline Vulkan forbids the explicit buffer
layout there. Preserve canonical semantic identity and buffer decorations. Whole
aggregate transfers convert through same-typed leaves under the shared traversal,
target-descriptor and word budgets; do not reuse a decorated type or change a
resource's byte layout to avoid the conversion. Zero initialization and robust
load-zero/store-discard retain their existing control-flow owner.

Expression/control depths remain64/32 for jobs and18/4 fixed. The parser's
compound ceiling2C+1 bounds malformed continuing syntax too. Re-measure per-function
frames and derive conservative combined parse/sema/emission call paths whenever
recursive owners change. Caller capacity and spare workspace never increase depth.

The emitter's local EMIT macro only serializes a bounded typed operand array and
propagates failure; it is not an IR or dispatch framework. Preserve MIT notices,
source provenance and the separation of developer tests from independent acceptance.

## Module functions and selected builtins

One function record owns ranges in shared syntax/IR/local storage. Parse all
signatures before resolving bodies. Formals are separate immutable typed values,
not mutable-local records or entry interface variables. Reserve all target callee
IDs before emission and emit each validated body once. Never recurse over callee
bodies or replay runtime source on the host. Whole-module iterative cycle checking
uses resolved expression callee slots, including non-emitted calls.

Reserve contiguous typed user-call arguments only on emitting visits, before
nested arguments, then fill once in source order. Helpers receive fresh function
locals and cannot capture entry locals/builtin inputs. Every user call may write
storage; no load cache crosses it. Scalar builtin names use ordinary lexical and
module lookup before the closed typed registry. Clamp lowers to safe min/max
composition, and constant bounds receive mandatory validation even in skipped
expressions. Save entry errno in every public wrapper and restore it on every exit.

Stream parameter and argument operands into checked private staging, with the
separate 16-bit instruction count guard before writes. No operand-count VLA or
arbitrary arity cap. Formal debug names are not emitted under the 4P word ledger.

## Aggregate layout, values and projections

Keep nominal type identity separate from structural layout. Resolve lexical and
module declarations before every predeclared spelling, including constructors.
Use one constructor path, not name-specific exceptions. Preserve source-order
argument snapshots and no load caching across calls/stores.

Use checked uint64 size/alignment/stride/offset arithmetic before target narrowing.
Runtime-tail types cannot silently become by-value constructible values. Typed
constant DAGs and access paths retain their own IDs/pool bounds; zero aggregates
must not force recursive host flattening. Every indexed subobject is guarded
before pointer formation, with independent load-zero/store-discard behavior.

Typed reflection publishes the declared graph/resource closure atomically. Legacy
projection failure identifies an unrepresentable declaration rather than silently
omitting it. MEASURE and each output selection still perform full private emission
and honor the same capacities. Preserve each frozen version's symbol and record
contract; new exports require an explicit additive versioned facade. No private
cross-file helper or test hook becomes a public export.

## Explicit image sampling

The common image semantic owner selects depth/color result width, sampler kind,
integer depth LOD and constant offset overloads. Targets consume typed operands,
not source spellings. Keep optional offsets as checked constant IR identities,
including const evaluation in skipped branches; never pass a runtime temporary
where the native instruction requires an immediate offset. Comparison operands
keep source order: the sampler compares the supplied reference to sampled depth.
Sampling layers clamp; normalized coordinates and LOD retain sampler semantics,
separate from guarded integer texel accesses. Preserve DEPTH/COMPARISON metadata
for runtime admission instead of inferring sampler choices from emitted text.

Implicit image sampling remains fragment-only through the bounded source call
closure, including dead syntax and helper calls. Derivative call sites require
uniform control, not uniform coordinates. Their results are potentially
nonuniform even for constant coordinates; use the existing uniformity summaries
and scratch, without treating a sample as a uniformity proof for later calls.
Explicit-level image operations retain their separate stage/control contract.

Boolean reductions belong to the same typed builtin signature/constant/IR owner.
Admit only bool scalars or bool vectors and return scalar bool. Evaluate the
complete operand once, including all vector-constructor effects, before reducing
saved values. Constant reductions project the existing DAG; preserve override
stage and required dependencies. Scalar targets copy the operand; vector targets
use explicitly mapped logical reductions and never infer numeric truthiness.
