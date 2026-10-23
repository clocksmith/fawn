# C compiler milestone 8: scalar numeric meaning and constant declarations

Frozen for implementation and independent evaluation, 2026-10-23. Not yet implemented or accepted.
Baseline: accepted M7 `7478e5d071a1ea3745d6ad9725a62a29c04d84b3`.

## Owner and retained boundary

Own scalar type/materialization, evaluation stage, numeric literals and operators,
scalar constructors/bitcast, and module/local scalar const declarations in the
existing shared C lexer/parser/sema/IR/SPIR-V pipeline. This is one reusable owner,
not six shader recognizers or a complete-WGSL claim. Its normative language source
is the [21 October 2026 WGSL CRD](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
especially sections 3.5, 5, 6.1-6.2, 7.2, 8.1, 8.5, 8.7-8.10, 8.19-8.20,
9.2-9.3, 12, 15.7, 17.1-17.2 and the scalar max overloads.

The retained trace is lexer/token/parser_expr/parser_decl, sema/sema_typeutils/
sema_expr, ir_const_eval/ir_builder, and emit_spirv_fn under
`runtime/zig/src/compiler/wgsl`. These are ownership references, not a numeric
oracle or permission for mechanical translation. C calls no Zig implementation.
Real consumption remains compile_job -> C CLI -> unchanged emitter consumer.
Native Vulkan translation metadata, runtime integration and physical execution
are separate owners.

Keep public ABI1/v2/job headers, enum values, result layouts and exported symbols
unchanged. Storage elements and flat uniform members remain u32/f32. i32 resources
are rejected because existing reflection cannot represent them; bool resources
are rejected as non-host-shareable, not reinterpreted as u32. No signed resource
layout, f16, aggregate values, helpers, aliases, overrides, const functions,
const_assert, multiple entries, workgroup memory, collectives, pointers, textures,
new math builtins, new targets, or production-provider selection is admitted.
The existing scalar max becomes valid for the new scalar numeric types/stages.

## One grammar, explicit capacities

Remove grammar/type/scope-only `extended` gates. Every public adapter uses the
same admitted syntax and meaning; only actual source/token/node/statement/depth,
IR/value/word/workspace and reflection limits may differ. ABI1 still cannot
publish uniform/struct reflection. A valid new program may exceed a fixed
adapter's real capacity; report the named budget, not an invented dialect.

Preserve every previously accepted program and its observable meaning, reflection
and publication contracts. Preserve old 320-node/18-depth/16-statement/4-control,
385-IR/386-value/4096-word fixed-adapter capacity, exact leading global-x alias
exemption, and old uncharged outer-if comparison accounting. Newly admitted
source is not entitled to a larger fixed IR/word budget. A historical negative
case intentionally lifted here is recorded by name without modifying its sealed
fixture or old receipt. Invalid syntax/types do not become successful through
reclassification. Equivalent admitted source has identical current words across
APIs/modes when resource reflection permits it; historical binary identity is
not promised when typed constant folding or target IDs legitimately change.

## Scalar language and stage

Concrete scalar identities are bool, i32, u32 and f32. AbstractInt is signed
64-bit two's-complement; AbstractFloat is finite IEEE binary64. Abstract types
exist only in semantic constant values, never SPIR-V types or runtime locals.
No implicit conversion between concrete numeric types or to/from bool occurs.
Apply the dated feasible conversion/ranking rules, not a C usual-arithmetic
conversion or expected-type propagation into an already-typed abstract subtree.
Default let/var materialization is i32 for AbstractInt and f32 for AbstractFloat.
An unannotated const retains its inferred abstract type. Concrete annotations
materialize only after evaluating the initializer at its selected type/stage.

A const reference remains const-stage. Let/var, resource and input reads remain
runtime-stage even when their initializer happens to be constant. Keep static
type, stage, constant payload, semantic value ID and mutable-local identity
separate. Validate names/types/mutability in all source, including dead statements
and skipped operands. Do not equate that validation with eagerly evaluating every
numeric expression: const short circuit follows section 8.1.1. Skipped RHS numeric
evaluation is suppressed unless needed for type/materialization or a mandatory
shader-creation error. For example, `false && (10i < i32(5*1000*1000*1000))`
succeeds, its true counterpart fails, while missing names and invalid types still
fail. Mandatory constant integer divisor/count checks are not skipped.

Literal grammar admits decimal and hexadecimal integers with no suffix, i or u;
decimal floats with point and/or exponent or f suffix; hexadecimal floats with
point and/or binary exponent and optional f suffix where grammar allows it.
Leading-dot/trailing-dot decimals, signed exponents, uppercase hex/exponent
markers and surrounding unary minus are included. h/f16 remains unsupported.
Reject malformed suffixes/digits/leading-zero decimal integers and out-of-range
literal values without truncation. Integers retain their actual declared width.
For floating literals only, following section 3.5 choose truncation toward zero
beyond 20 significant decimal or 16 significant hexadecimal digits, before conversion. This is not a shorter
source-token limit. Decimal conversion selects nearest-even; hex literal
representability rules, including exact f32 representation where required, remain
mandatory. The exact-hex rule applies to f-suffixed literals: an unsuffixed hex
literal becomes AbstractFloat and later f32 conversion uses the normative
sections 6.1.2/15.7.6 rounding rule. The section 6.2.1 example comment claiming
otherwise conflicts with those normative rules and is not the acceptance rule.
Named and direct AbstractFloat values do not acquire spelling-dependent meaning.
Preserve negative zero, normal/subnormal boundaries and source spans.
No locale-dependent parsing, strtod/strtof or silently f32-rounded AbstractFloat.

Selected scalar operation matrix (automatic abstract conversions apply first):

| Operation | Admitted operand types |
| --- | --- |
| unary - | AbstractInt, AbstractFloat, i32, f32; not u32/bool |
| unary ~ | AbstractInt, i32, u32 |
| unary ! | bool |
| +, -, *, /, % and max | same numeric scalar type |
| ==, != | same scalar type, including bool |
| <, <=, >, >= | same numeric scalar type |
| &, \|, ^ | same integer scalar type; &, \| also eager bool operations |
| &&, \|\| | bool, short-circuiting |
| <<, >> | AbstractInt/i32/u32 left with u32 count (abstract count may materialize) |

Follow WGSL expression grammar rather than C precedence. Mixed bitwise operators,
bitwise/arithmetic combinations, mixed &&/|| and chained comparisons require the
parentheses demanded by that grammar. Shift operands are unary expressions;
unparenthesized additive operands or chained shifts do not acquire C meaning.
Compound assignments add /=, %=, &=, |=, ^=, <<=, >>= wherever the corresponding
scalar overload applies. ++/-- cover i32/u32. Preserve target/index-before-RHS,
once-only target/index evaluation, and independent robust read/write guards.

Concrete integer +,-,* and permitted unary negation use width-limited WGSL
semantics, without C signed overflow. Abstract arithmetic rejects unrepresentable
results, subject to the operator's explicit signed-minimum negation rule.
Integer division truncates toward zero, signed remainder follows its dividend.
Constant zero RHS is a shader-creation error even beside a runtime numerator.
Constant signed-minimum / -1 and % -1 follow the dated mandatory-error rule.
Runtime zero divisor yields the numerator for division and zero for remainder;
runtime signed-minimum/-1 yields the numerator for division and zero remainder.
A constant concrete shift count >=32 rejects even with runtime left. Abstract
shifts use the actual u32 count, without modulo: right shift at/above 64 yields
the sign fill; left shift obeys its leading-sign-bit overflow rule (only zero
can survive a count at/above 64). Runtime concrete counts are reduced modulo 32.
Constant left-shift overflow rejects by the signed/unsigned leading-bit rules;
ordinary concrete addition wrapping does not waive this restriction. Signed
right shift sign-extends; unsigned right shift zero-fills. All emitted potentially
invalid division/remainder/shift instructions receive safe operands first.
A select over an already-invalid division is not a guard.

Scalar `bool`, `i32`, `u32`, `f32` constructors admit zero or one argument with the
specified overloads, including bool/numeric explicit conversion. Constructors,
max, bitcast and type annotations resolve through normal module/lexical scope;
user shadowing cannot be bypassed by spelling dispatch. Explicit abstract-to-int
conversion is range checked, not a concrete wrap. Concrete i32/u32 conversion
preserves low 32 bits. Numeric-to-bool tests zero; bool-to-numeric yields zero/one.
Float-to-integer conversion clamps then truncates using the nearest source-float
representable target integer: f32 u32 upper 4294967040, i32 upper 2147483520 and
lower -2147483648. AbstractFloat constructors instead use binary64 source
representability, so `u32(4294967296.0)` clamps to 4294967295, unlike its f-suffixed
source form. Constructor argument materialization follows the actual overload
ranks, not a catch-all direct source-to-destination cast. In particular, the
f32 constructor takes a concrete scalar argument; its preferred AbstractInt
argument materialization is i32. Thus `f32(4294967295)` rejects, whereas
`f32(4294967295u)` and `let x:f32 = 4294967295` admit by different type rules. Runtime NaN conversion selects zero as a permitted indeterminate
result; normalize before OpConvertFToU/S, never after invoking undefined conversion.

`bitcast<T>(e)` admits i32/u32/f32 scalar target/source and their identity overloads.
Abstract arguments follow actual overload/materialization rules, including the
special range-checked bitcast<u32>(AbstractInt), never reinterpret a 64-bit abstract
payload as 32 bits. bool, vector and f16 bitcast remain rejected. Emitted OpBitcast and the instruction model preserve all 32 runtime payload
bits, including signed zero/nonfinite payloads, without claiming universal
physical-device payload behavior; evaluated
constant bitcast producing NaN/infinity rejects, even if immediately cast back.
A literal-initialized let still evaluates its const initializer; a later read of
that let is runtime stage. Exact emitted OpBitcast/oracle payload tests do not
claim universal physical-device preservation of every nonfinite payload.

## Floating evaluation and supported compiler host

Choose nearest-even per-operation binary64 for AbstractFloat and per-operation
binary32 for concrete f32 constants, without reassociation or contraction. Reject
evaluated nonfinite const intermediates and out-of-range required materialization.
Runtime f32 operations remain distinct typed target operations; runtime nonfinite
and subnormal behavior is limited by the dated WGSL guarantees, not a new universal
GPU bit-identity claim. Floating remainder follows the selected WGSL operation,
not integer remainder or floor-based modulo.

Supported compiler-host proof is Linux x86-64, GCC14.2, IEEE binary32/binary64.
A bounded host-FP implementation is permitted, with compile-time representation
checks and -fno-fast-math, -ffp-contract=off, -frounding-math and width-forced
operations. It must save, hold and restore the calling thread's complete floating
point environment on every success/failure, including enabled traps, rounding,
exception flags and SSE FTZ/DAZ state; establish gradual underflow internally.
No process-global locale/environment changes. State/query failure is explicit
INPUT_ERROR before publication, never silent alternate arithmetic. Complete any
fallible environment restoration before the sole output-word publication point.
An unexpected restoration failure preserves output but cannot claim the caller
environment was restored; report that specific host failure, attempt the bounded
saved-state restoration path, and do not qualify that host/path. Record actual
libm/libc dependencies and prove no hidden allocation, caller state leakage or
cross-thread interference. This does not claim another compiler host qualified.
Software-integer numeric implementation is also permitted if it meets the same
selected rounding, numeric and no-allocation contracts.

## Constants, module order and attribute consumers

Admit any number within checked storage of `const name [: T] = expr;` at module
or function scope. Local const occupies its normal statement/scope and cannot
reference runtime values. Module declarations may be interleaved before/after
the single entry, including resources, flat structs, constants and empty module
declarations. Module names are hoisted; resolve all declarations, initializers,
annotations and dependencies independently of declaration order. Reject duplicate
module names, undefined references, self/indirect dependency cycles, invalid
annotations and invalid unused constants. A skipped expression cannot hide a
namespace/dependency cycle. Local names become visible after initialization and
use ordinary nearest-scope lookup. No const is mutable.

The existing @group, @binding and @workgroup_size positions accept scalar integer
const expressions, including forward module references. Their final values must
fit the existing nonnegative group0..3/binding0..31 and dimension/product1..256
envelopes. Nonconstant/bool/float attributes reject. Signed runtime array indices
are admitted: negative const index is a shader-creation error; negative runtime
index is robust OOB. Positive i32 indices and u32 indices use the same per-access
bounds owner, with wrap-before-check where arithmetic is concrete.

## Checked storage and target bounds

No new public capacity record or ABI. Derive module constant capacity C=E+1 from
configured charged scalar nodes E: every constant initializer owns at least one
node, except the retained exact leading annotated literal-u32 module constant.
That old declaration/initializer remains uncharged in every adapter where it was
previously accepted. All new module initializer/attribute nodes consume E, while
module declarations do not consume function-statement budget S. Retain bare
integer attribute accounting so old accepted limits do not shrink. Local const
consumes one S and its initializer nodes. Checked physical syntax reserves the
legacy exempt nodes separately; do not charge them by quietly reducing E.

Constant syntax/value/state and any dependency worklist are explicit checked
caller-job slices. Use iterative dependency traversal, never recursion tied to
module declaration count. Compact copied syntax/scope names into validated source
spans to preserve fixed workspace <=77760 and ABI1 GCC14.2 -O2 frame <=77824 bytes.
Every other library frame remains <=8192 bytes. Preserve the M7 64/32 job and
18/4 fixed expression/control depth envelopes and combined recursion bound;
module dependency count adds no recursive stack depth. No library
malloc/realloc/free, VLA, input-sized stack object or second workspace.
Token lifetime still ends before its phase-unioned lowered storage is reused.

Keep the frozen job I=1+4E+8S typed instructions and I+1 semantic-value slots.
The final operation ledger must prove that bound: a scalar numeric operation is one IR operation,
with target guards/conversions expanded by the backend, not charged as imaginary
source nodes. Add explicit checked C-record/worklist byte products and alignments.
Target reservation becomes a conservative 256+32I bound, checked including the
final SPIR-V header bound; every actual reservation also checks. The backend's
per-op reservation ledger must prove <=32 new IDs, including constants, guards,
conversion clamps and labels. Output words retain their independent capacity.
Fixed adapters keep 385 IR operations, with newly admitted over-budget programs
rejected atomically; all old accepted sources still fit. Compactness is not a
waiver of explicit array, alignment, ID or word checks.

Retain public ABI/exact-size/null/disjointness preflight, failure status classes,
zero count/reflection and unchanged output words on every failed call. Preserve
workspace reuse/concurrency and CLI acquisition/rollback fault behavior. Existing
CLI capacities, options, optional sidecar and reflection JSON schemas are unchanged;
no new CLI allocations are required solely for this owner. Re-measure all private
layouts and GCC frames plus the expression/control/dependency combined stack bound.

## Independent acceptance before implementation

Freeze independent cases/oracles before candidate inspection. Include:

- Every scalar type/stage/operator row, signed/unsigned edges, abstract overflow,
  materialization ranking, bool distinctions, literal forms/malformed tokens,
  decimal/hex rounding boundaries, subnormals and signed zero.
- Const versus let/runtime pairs for divisor zero, signed-minimum division/remainder,
  shifts/counts/overflow, constant short circuit and nonfinite/overflow materialization;
  emitted instruction execution must expose invalid-operand-before-select bugs.
- Conversion versus bitcast, f32 clamp boundaries and NaN selection, concrete
  wrapping, abstract range rejection, scope/type/builtin shadowing and mutable
  snapshots. Independent exact integer/bit oracle and justified f32 comparisons.
- Multiple/forward/unused constants, diamonds/long dependency chains/cycles,
  declarations after entry, local const scope/runtime rejection, attribute constant
  consumers, signed negative/OOB indices and nested robust accesses.
- All APIs/CLI modes on small shared syntax and exact old limits, named rejection
  lifts, deliberate new fixed-IR exhaustion, job layout overflow/alignment,
  short buffers/redzones, success-failure-reuse/concurrency, no allocation, strict
  C/C++ native consumers, preflight and file-publication failure tests.
- Caller rounding modes, flags, enabled traps and FTZ/DAZ on success and failure;
  concurrent jobs with distinct environments; sanitizer and source-span checks.
- All retained accepted M1-M7 behavior, immutable historical suites/receipts, target
  validation explicitly under Vulkan1.1, unchanged emitter consumer, and stage-
  separated execution/corpus evidence. Newly expected positives are additive.

Six unchanged anchor paths: `bench/kernels/package_image_rgba_invert.wgsl`,
`bench/kernels/matrix_vector_mul_32768x2048_f32.wgsl`,
`bench/kernels/compilation-corpus/moderate_rope.wgsl`,
`bench/inference-pipeline/kernels/rope.wgsl`,
`bench/inference-pipeline/kernels/attention_small_f16kv.wgsl`, and
`bench/inference-pipeline/kernels/attention_decode_online_f16kv.wgsl`.
Their verified hashes/dependency trace are in the next-owner evidence. Freeze
independent separately packed-resource execution for each, retain all 98 ledger
paths and discover later failures rather than promising a coverage percentage.
The two rope paths are one language family, and the attention filenames do not
claim f16 support. sqrt/exp/clamp/helpers/vectors/collectives remain later owners.
No CPU oracle or SPIR-V validation result establishes GPU/runtime qualification,
performance parity, other target support or completion of the Doe migration.
