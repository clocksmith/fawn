# C compiler milestone 4: typed scalar expressions and immutable locals

Contract frozen before implementation on 2026-10-23 against accepted M3 commit
`82002022979511efc434f56eab4295327f267db2`, before implementation. This is a
compiler language-owner increment, not source-specific recognition, a complete
WGSL frontend, runtime migration or hardware qualification.

## Selection and real sources

The [M3 corpus ledger](../../../docs/c-migration/corpus-coverage-m3.json) records
98 paths / 91 unique contents: one unique residual source admitted, 87 lexical
and nine parser first failures. Later phases for rejected sources are not reached.
Source inspection groups prerequisites into expressions/locals, entry interfaces,
struct/uniform layout, loops/mutable locals, helpers, workgroup synchronization,
graphics/textures and separately gated subgroups. First errors do not exhaust
missing features. This increment selects scalar value expressions and immutable
local values because they are foundational and admit two distinct real compute
families without pretending that lexing more operators implements those others:

- `bench/kernels/compilation-corpus/simple_scale_f32.wgsl`, SHA-256
  `21f18f6bad8c79bd9246f27b0eb25b714f666b7c8ffbd4f56ae0841589d4cab3`.
- `bench/kernels/compilation-corpus/simple_relu.wgsl`, SHA-256
  `a79c6bb744a3c13f734f1e17a2f9f617c05aac7ae3d64f67c9cc98945f459acc`.

Compile both unchanged and consume their fresh output through the existing emitter
consumer. This is source coverage, not adoption by their existing MSL benchmarks.
Preserve the M3 binary/library and failing diagnostics before implementation.

## Admitted expression and local-value surface

Retain all M1/M2/M3 positive behavior, public ABI/version/limits, CLI contracts,
reflection, source diagnostics, checked output publication and independent calls.
Add bounded scalar expression trees to storage-assignment RHS and scalar `let`
initializers:

- Primaries: existing robust storage loads and u32 literals/module constant;
  scalar local names; direct invocation `.x`; decimal float literals below;
  parenthesized admitted expressions; and `max(a, b)` with two same-type scalars.
- Binary `*`, then left-associative `+` and `-`, with ordinary precedence.
  Operators accept matching u32 or f32 operands, with only the literal
  materialization described below. u32 arithmetic wraps modulo 2^32.
- Immutable `let name = expression;` or `let name: u32 = expression;` or
  `let name: f32 = expression;` in the existing entry/block scopes. Evaluate once
  at declaration, not again at each use. Reads and writes remain in source order.
  A previously loaded local therefore keeps its value after a storage write.
- Local scope begins after its initializer, ends at the enclosing brace, and
  does not escape a branch. Reject undefined/use-before-declaration, incompatible
  annotations, writes to locals and duplicate declarations in one scope.
  Shadowing any visible user-declared name is explicitly unsupported in this increment;
  disjoint sibling scopes may reuse a name. Local names must not collide with
  retained module/resource/entry/invocation/legacy-alias names.
- Scalar u32 locals may be index-base atoms in the retained M2 index/condition
  grammar; offsets and comparison RHS remain literal/module constants. No
  arbitrary expression index or new condition grammar is silently admitted.
- Retain exactly the existing optional leading scalar alias `let name = PARAM.x;`
  and its old name rules and statement accounting. It consumes neither a new
  expression node nor one of the 16 body statements. Annotated/parenthesized
  variants and further lets are ordinary counted scalar lets. No vector-valued
  general lets are added.
- Predeclared names are not user declarations for the no-shadowing limit. A new
  local may use `max` or a contextual type name. Lookup must respect that scope:
  a value named `max` does not become callable; an explicit `u32`/`f32` annotation
  must reject if that type name is shadowed by a visible user value. Inferred
  materialization does not require a spelled type-name lookup. Retain old alias
  name behavior rather than retroactively imposing new-local restrictions.
  Builtin lookup must respect names: a value named `max` does not become callable.
  Other calls, constructors/casts, assignment operators, mutable locals, unary
  operators, division, remainder, bit operations, structs, loops and new entry
  interfaces remain explicitly unsupported.

## Decimal and numerical boundary

Admit decimal float tokens `[0-9]+.[0-9]+` with an optional `f` suffix, at most
nine total digits excluding the dot/suffix. No exponent, hexadecimal, leading-
point/trailing-point, `h` suffix or larger digit form yet. Reject unsupported
forms rather than splitting them into a different expression or truncating.
A decimal token denotes its exact nonnegative rational value. Direct
materialization to f32 uses round-to-nearest, ties-to-even, independent of locale
or host rounding mode (selected implementation policy, not mandated WGSL rounding); no ambient `strtof` or binary64 intermediate. This bounded
range contains zero and normal finite binary32 values only. Existing u32 literals
retain required `u` suffix and checked 32-bit range.

Unsuffixed floats are not eagerly substituted into abstract arithmetic. A lone
float literal may materialize into an f32 store or f32 scalar let (including
an inferred f32 let). An f32 arithmetic/max node must contain a runtime-typed
operand at that node; a float-literal sibling materializes to that concrete type.
A scalar let is a runtime-typed value after its initializer. Constant-only f32
arithmetic/max subtrees, including explicitly suffixed forms, are rejected as
unsupported until constant-expression evaluation is owned. Thus `data[i] * 2.0`
and `let x = 0.1; data[i] = x * 2.0;` are admitted, but
`data[i] * (0.1 + 0.2)` is not silently evaluated with wrong abstract precision.
Also reject `data[i] * (16777217.0 - 16777216.0)`, whose inner value would
incorrectly become zero under premature binary32 rounding instead of one.
Concrete u32-only arithmetic/max remains admitted with wrapping semantics.

Preserve expression grouping; do not reassociate or contract multiply/add in the
compiler. For finite normal values and zero, max selects the larger numeric
operand; equal operands may select either signed zero. Numerical acceptance uses
finite exactly representable cases and independent operation-level binary32
rounding tests. No new NaN, infinity, subnormal, signed-zero-bit-identity or
physical-device numerical qualification is claimed. This is a declared subset
of the [WGSL numerical/type rules](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
not a replacement specification or universal binary32-hardware promise.

## Representation, ownership and limits

Use parsed expression nodes, typed value IDs and lexical local records; target
emission consumes semantic operations, not names or source templates. Each load
and store retains its own resource/index guard. Do not hoist a load across a
branch, return or intervening write. Local values must dominate every emitted use;
no Phi or pointer may refer to a dead or sibling block. Validate unreachable
source even when the emitter omits it.

Bound the whole function to 64 RHS/initializer expression nodes, with expression
depth at most 16: a primary has depth one, each binary/max node adds one
to its deepest child, and each parenthesis adds one. Existing index representation
is separate. Parentheses need not allocate a value node. Existing 16 statements and
four nested-if levels remain. Derive and document a safe checked IR/value bound
from these combined limits, replacing the old two-operand derivation. Check the
4,096-word target cap and report exhaustion without output. Measure stack/frame
growth and depth, not an unqualified no-allocation benefit. This increment keeps
per-call bounded stack ownership and no mutable globals or retained pointers;
full compiler allocation policy remains a later explicit job-storage owner.

## Acceptance and bounded effort

One implementation worker owns C owners and developer tests; one independent
worker freezes black-box cases/oracle controls before inspecting changes. Parent
reviews integration. No parallel full rewrites or extra toolchain installation.
Preserve current receipts and pinpoint intentionally lifted old negative cases;
never broadly disable the retained suites or rewrite their historical outcomes.

Require unchanged real sources, renamed/declaration-order/comment variants,
precedence/parentheses/associativity, u32 overflow, mixed-type rejection,
local snapshot/scope/dominance, branch early returns, max operand order, unequal
resource lengths, zero/last/first-OOB/UINT32_MAX invocation indices, unchanged
inputs and deterministic reflection. Exercise decimal ties and neighboring values
with an exact independent rational oracle and locale/rounding-mode independence.
Test unsupported literal forms and constant-only f32 subtrees explicitly.

Require API/CLI equivalence, native C/C++ source builds, explicit Vulkan 1.1
validation, independent instruction execution with new opcode controls,
concurrency, capacity boundaries, guard pages, malformed/mutated source and
ASan/UBSan. Keep no-clobber CLI failure checks. Update the same corpus ledger with
unique output paths and separate each phase; do not rerun full gate main or count
old prebuilt modules as fresh source success. Stop at this bounded language owner,
commit its accepted result locally, then select the next dependency from evidence.
Production/main, historical evidence and actual GPU/runtime gates stay unchanged.
