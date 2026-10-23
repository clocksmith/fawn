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

numeric.c owns locale-free bounded literal parsing, rounding, const arithmetic and
checked conversions. Lexer/parser preserve raw literal spans, not floating host
values. Const evaluation checks every evaluated intermediate. Skipped short-circuit
numeric evaluation is distinct from required name/type/dependency validation and
mandatory divisor/count checks. Runtime operators never call the const evaluator.

The pipeline owns one NumericEnvironment. Save/hold/normalize caller rounding,
traps, exception flags and SSE subnormal modes; restore before publication on both
success and failure. Use the frozen strict FP build flags and qualified IEEE host,
with no contraction/reassociation or process-global locale changes. A failed
restore is a specific host error, not a claim of successful state preservation.

Typed IR uses concrete scalar types only. Keep safe-operation guards in the
emitter: normalize integer denominators, mask shifts, and sanitize/clamp float
conversion inputs before potentially invalid target instructions. Do not select
over an already-invalid result. Signed robust indices test nonnegativity before
combining with the independent resource length check. Target bitcast semantics do
not prove universal physical NaN/subnormal payload preservation.

## Namespace and caller storage

Module declarations are hoisted and source-order independent. Constant resolution
is iterative: unresolved ConstantSyntax value storage holds scan/parent links;
only a fully resolved declaration publishes a numeric payload into that union.
Scan all initializer references even in unused/skipped expressions. C=E+1 follows
from one node per new initializer and one exact legacy exemption. No recursive
stack proportional to module declarations or private heap worklist is allowed.

Local scopes save/restore visible record counts. A binding enters scope after its
initializer; same-scope duplicates reject and nested/module shadowing is ordinary
WGSL lookup across all adapters. Type names, constructors, max and bitcast must
resolve normally before builtin dispatch. Immutable runtime bindings retain IDs,
not ASTs to replay; const bindings retain typed numeric values.

storage.c owns the frozen job formula I=1+4E+8S, I+1 semantic-value entries, S+1
local/scope entries, E+1 constants and the target bound256+32I. Each typed slice
checks SIZE_MAX/alignment, every semantic append checks capacity, each target
reservation checks its bound, and word serialization remains independent. Record
an operation-by-operation proof when adding IR/target operations; do not enlarge
bounds after failures. Semantic IDs alone index value maps, never target IDs.

Fixed adapters retain source/token/statement/expression/depth/IR/word limits and
exact leading-constant, leading-global-x-alias, old-if-comparison and bare-attribute
accounting. Compact private names create space for constant metadata without
expanding public headers. Actual measured workspace must remain<=77760 and ABI1
frame<=77824; every other library frame<=8192. See README for current measurements.

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

Resource/member identity is a typed declaration index, not spelling. Frozen public
resources remain u32/f32. Uniform layout and minimum extents are distinct from
storage robustness. Each array access guards its own resource before even forming
an element pointer; OOB load zero can be a valid outer index. No combined-length
shortcut suppresses an otherwise valid outer operation.

Expression/control depths remain64/32 for jobs and18/4 fixed. The parser's
compound ceiling2C+1 bounds malformed continuing syntax too. Re-measure per-function
frames and derive conservative combined parse/sema/emission call paths whenever
recursive owners change. Caller capacity and spare workspace never increase depth.

The emitter's local EMIT macro only serializes a bounded typed operand array and
propagates failure; it is not an IR or dispatch framework. Preserve MIT notices,
source provenance and the separation of developer tests from independent acceptance.
