# M12: typed numeric and geometric builtins

Reviewed contract for installation, 2026-10-23. Not implementation acceptance.
Accepted M11 baseline: `f01effdf070edf2f25cf0f2016c995df1e4d3bab`.
Accepted tree: `edd985a9105432bfcad6c3dbe387cfc0e1f66fee`.
All 30 compiler source-manifest files were verified against accepted Git blobs
and current working bytes. Source manifest SHA-256:
`b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af`.
The accepted-M11 dependency is closed; independent M12 case/oracle sealing is
still required before implementation starts. No M12 implementation is present.

Companions: [ledger](MILESTONE_12_LEDGER.md),
[independent evaluation design](MILESTONE_12_EVALUATION.md),
[numeric interpretation](contracts/m12_numeric_interpretation.md), and
[source bindings](contracts/m12_source_inputs.json). The numeric interpretation
preserves contrary upstream behavior rather than claiming universal agreement.
Prior draft revisions and their seals remain unchanged in the retained planning
package. [Witness hashes](contracts/m12_witnesses.json) retain trace-time notes:
their M11 prerequisite is now satisfied, but they are not M12 execution results.

## Selected authority and limits

One C11 compiler pipeline gains the full legal overload family of existing
min/max/clamp/exp/tanh/sqrt and dot/length/distance/normalize/cross within the
already admitted AI/AF/i32/u32/f32 type envelope. AI and AF mean AbstractInt and
AbstractFloat; their names cannot be spelled as source types. There is no second
numeric type system, separate module-initializer evaluator, legacy dialect,
source-pattern shader substitute, or retained-Zig fallback.

Excluded: f16, matrices, additional numeric builtin families (including floor), packed dot,
overrides, aliases, pointers/atomics, texture/graphics/subgroup features,
additional entry points, other targets, physical execution and runtime adoption.
Existing M11 source and uniformity rules remain a prerequisite, not redefined.
No public ABI/header/export/CLI-schema change, default provider change, pool
growth, dependency installation or public publication is proposed.

The real-source selection remains the unchanged files in
`contracts/m12_witnesses.json`. Fifteen dot and two vector-clamp first
errors are NOT seventeen future admissions. The two fluids advection files still
need excluded floor. Geometry witnesses with graphics/textures remain blocked
whole sources. Distance has no direct unchanged real-source witness in that
bounded scan; it closes the same selected length-of-difference family.

## Closed overload family

Let N be 2, 3 or 4; A={AI,AF,i32,u32,f32}; F={AF,f32}.

| Kind | Arity | Argument shape and element type | Result |
|---|---:|---|---|
| min, max |2| same scalar A, or same vecN<A> | argument type |
| clamp |3| same scalar A, or same vecN<A> | argument type |
| exp, tanh, sqrt |1| scalar F or vecN<F> | argument type |
| dot |2| matching vecN<A> | element A |
| length |1| scalar F or vecN<F> | element F |
| distance |2| matching scalar F or vecN<F> | element F |
| normalize |1| vecN<F> | argument type |
| cross |2| matching vec3<F> | argument type |

Each is const-capable and requires a used result. No scalar-vector splatting,
width conversion, mixed concrete types, bool, array or struct overload exists.
Same-shape AI arguments may convert to AF for floating-only families. Abstract
arguments may materialize into the selected concrete same-shape overload using
existing rank/representability rules. Integer concrete arguments do not silently
convert to f32. Return context must not retroactively turn an already evaluated
abstract subtree into a different numeric computation. Existing let snapshots
are runtime-stage values even when initialized with literals.

Keep exact arity; reject explicit type arguments and bare function-call
statements through the existing must-use behavior. A phony assignment such as
`_ = dot(...)` is a valid WGSL use, not a must-use violation, but general phony
assignments remain excluded by the retained statement owner (M9); this numeric
owner does not silently add that syntax. Retain existing admitted constructors,
bitcast, resource builtins and workgroupBarrier unchanged. Lookup order remains lexical
values/formals/inputs, module functions/nominal constructors/module values, then
predeclared names. A function or struct named dot, clamp or normalize is not the
predeclared builtin. Do not add source-spelling tests downstream of resolution.

The normative reference is the dated [WGSL CRD, 21 October 2026](https://www.w3.org/TR/2026/CRD-WGSL-20261021/),
especially §§6.1.2–6.2.4, 8.1, 15.2, 15.7 and 17.5. This contract's overload
table is a compact type notation, not a verbatim reproduction of the standard.

## Source evaluation, overload selection and stage

1. Resolve callee identity before reading the registry. Reject bad context/arity
   before indexing the bounded three-argument local array.
2. Visit actual argument expressions in source order through the existing
   sema_expression with the incoming evaluate/emit flags. Keep each resolved
   ExpressionValue (at most three) and its syntax ID. Do not replay a runtime
   argument after discovering a vector shape or shape-reducing return type.
3. Reject any non-scalar/non-vector shape and unequal widths before combining
   leaf types. Use the existing common-type/rank owner; floating-only families
   promote an all-AI leaf to AF. Runtime participation requires the existing
   concrete default/materialization rule. Commit at most the selected interned
   operand vector type; result is that type or its scalar leaf as the table says.
   No speculative overload-owned type or constant records.
4. Materialize the already obtained values into the selected operand type.
   This does not revisit source expressions. A dynamic load or helper call is
   already a captured semantic value. No caching crosses a source call/store.
5. Apply mandatory clamp-bound validation below, then either fold once using
   typed constants or append one runtime IR_BUILTIN. For runtime emission,
   reserve exactly arity O entries once, after source argument evaluation, and
   fill with stable semantic value IDs. Ensuring a constant value may add its
   ordinary IR_CONSTANT, but no nested argument evaluation occurs at this point.
6. Runtime expansion uses those IDs only. Swizzles and argument helpers execute
   once, in source order, regardless of target lane count. Short-circuit skipped
   RHS emits no runtime effects; name/type/dependency validation still occurs.

One small builtin semantic helper can replace the present scalar-only helper
without disturbing user-call and constructor resolution. Preserve all module
dependency scanning, including dead/skipped expressions, and iterative constant
declaration resolution. A module const, local const and const subtree use the
same fold owner; no special module spelling/evaluator or runtime fallback.

## Mandatory clamp checks are independent of folding

When both lower and upper bounds are const-expressions, compare every lane after
selected-overload materialization, even if the value argument is runtime, the
whole call is dead, or enclosing short circuit has evaluate=false. One lane with
lower>upper produces a semantic shader-creation diagnostic at this original
clamp call, never an unsupported-capability result.

If a skipped const bound is not yet known, evaluate that bound through the same
constant path with evaluate=true, emit=false. This is required const validation,
not replay of an effectful runtime argument; runtime bounds are never forced.
Keep bounds' resulting ConstantValues for comparison rather than evaluate them
again per lane. Their real K/L charges remain visible; there is no uncharged
temporary aggregate DAG. All-bounds runtime/let cases are not const errors.
Mixed const/runtime bounds use runtime semantics, not a partial const check.

Runtime integer clamp is min(max(value,lower),upper), including reversed bounds.
Runtime float clamp uses the same allowed composition. Never emit target Clamp
instructions with potentially reversed bounds, and never compute a poisonous
Clamp then select around its result. Shape checks precede lane access.

## Shared constant and numeric owner

NumericValue remains scalar type+bits. ConstantValue remains typed inline scalar
or compact aggregate node. Project at most four lanes from existing ZERO/REPEAT/
COMPOSITE nodes; assemble at most one result vector node with N inline edges.
No array-sized temporary, hidden pool, heap allocation or recursive flattening.

Component-wise families call the existing checked scalar primitive once per
lane. Preserve scalar signed-zero choices, finite-result checks, conversion
rules, numeric environment and errno behavior. Do not copy libm entry logic or
floating-environment normalization into the vector owner.

Concrete i32/u32 dot: multiply and add through unsigned modular arithmetic,
returning the exact mathematical dot modulo 2^32 at const and runtime stages.
No signed C overflow, no constant-only rejection for product overflow, and no
integer-dot extension/capability requirement. Signed interpretation changes
only the final i32 value, not the low-bit computation.

AbstractInt dot: explicit selected interpretation N1 in contracts/m12_numeric_interpretation.md.
Compute the exact mathematical dot and range-check its final signed64 result.
Parent selected this route after independent text/CTS/Tint investigation found
no decisive upstream adjudication. Preserve the contrary Tint behavior and CTS
coverage gap; do not label this settled universal WGSL parity. The bounded
mechanism and distinguishing cases have passed design review; the independently
owned executable acceptance case/oracle seal remains required before implementation. All argument
subexpressions remain individually checked; no other numeric rule changes.

Selected deterministic const floating mechanism (an implementation choice, not
universal GPU bit semantics): use existing binary32/binary64 numeric primitives;
dot forms lane products and left-folds from the first product; length uses
sqrt(dot(x,x)), with scalar x*x; distance first forms lane differences then
length; normalize divides each original lane by length; cross uses the standard
cyclic two-product difference for each lane. Each evaluated primitive is finite
checked at the selected width. Do not seed a reduction with an extra +0 operation.
Reuse current sqrt numeric primitive and division, not a new host math dialect.
The independent oracle must distinguish this chosen const graph from WGSL's
permitted runtime accuracy set. Stable algorithms with a wider valid extreme
domain require a deliberate reviewed const-policy change, not ad hoc case fixes.

All finite const outputs retain type-width rounding. Negative sqrt, zero-vector
normalize and evaluated nonfinite results are creation errors. Underflow to a
finite subnormal or zero is not automatically an error, including libm ERANGE.
For skipped numeric evaluation, do not invent a new eager domain check beyond
the existing mandatory checks (clamp bounds, divisor/count rules, etc.). Runtime
nonfinite/domain/zero-normalize/subnormal latitude remains explicitly separated
from strict finite-domain accuracy cases; no exact NaN payload promise.

## Runtime target and uniformity

Keep SPIR-V1.3/Vulkan1.1 selected target. Floating dot may use OpDot; integer dot
uses vector IMul, N extracts and N-1 IAdds. Typed GLSL.std.450 Exp/Tanh/Sqrt,
Length/Distance/Cross/Normalize are emitted with actual result/operand types.
Scalar length/distance are legal, scalar normalize is not. Min/max/clamp reuse
safe scalar compare/select lowering per lane; they need no boolean-vector type
or guard/splat constant. The exact counts and import plan are in the ledger.

GLSL/std target validity is NOT proof that an actual backend meets WGSL numeric
accuracy. Independent decoded-instruction execution checks compiler intent and
the chosen deterministic model, while the separate WGSL interval oracle rejects
answers outside its allowed set. Physical driver numerical qualification stays
a later explicit gate; neither validator success nor a model calling libm can
claim it. Keep no-contraction/reassociation decisions explicit if the target
strategy changes; this draft adds no such decoration or new floating capability.

Resolved pure builtins keep M11 callee_map=0. Uniformity unions the dependency
of EVERY source argument plus existing caller control-flow behavior, even when
an argument is numerically multiplied by zero, lane-cancelled, or non-emitted.
No pure=>uniform shortcut or dependency reduction from constant folding. User
functions retain actual callee identity and summary requirements. Every new
builtin follows the already charged AST visitation path and same 2^26 analysis
budget, diagnostics and no-barrier fast path. No new uniformity implementation.

## Source responsibility factoring

- internal.h: rename the closed private scalar builtin identity to numeric
  builtin identity and append five kinds; four-word IR payload/record size stays.
- builtin.c plus a narrow builtin.h: single read-only 11-entry signature registry,
  shape/result classification, and typed constant projection/assembly adapter.
  It calls scalar numeric primitives; no second parser/resolver/float environment.
- numeric.c/h: retained scalar primitives, plus a bounded exact AI dot primitive
  only after N1 is sealed. Do not generalize literal parsing's 48-limb buffer
  into a new dynamic bigint framework for this four-lane operation.
- sema_builtin.c: selected-overload argument/stage/materialization/mandatory-check
  orchestration moved from sema_call.c. sema_call owns ordinary name dispatch.
- spirv.c: small typed builtin emitter using existing Emitter helpers; the sole
  registry's explicit target classification replaces enum-order import guessing.
  Do not split Emitter into a broad new callback or code-generation framework.
- constants.c/types.c/sema_expression.c: reuse existing owners; only extract
  genuinely shared narrow lane/shape helpers if necessary, with exercised callers.
- Makefile: add the new C translation units once; hidden visibility remains.

Any subsequent module factoring change must preserve these reviewed
responsibilities; moving ownership must not duplicate semantics. The reviewer checks
the registry is authoritative and no private helper becomes an eighth export.

## Failure, compatibility and acceptance gates

All seven exports, WORDS/REFLECTION/MEASURE combinations and CLI use one pipeline.
Keep existing capacity/error taxonomy, disjoint-span/stale-workspace checks,
atomic no-partial publication, source positions, numeric-environment restoration,
errno, concurrency and no-allocation guarantees. Incorrect overloads are semantic
errors; valid excluded capabilities stay explicitly unsupported; named pool
exhaustion is not a semantic error. Accepted old success cannot be lost to new
fixed reserves, enum-order target import, const-only wrap differences or blanket
rewrites of historical negatives. Historical files and receipts stay immutable.

Gate sequence: accepted M11 source identity and N1 interpretation/disclosure
are bound here; this contract and ledger are design-reviewed; seal the
independently owned executable cases/oracle separately, then implement. Acceptance requires independent source builds, actual
SPIR-V validation/decoding/execution, differential/mutation controls, native API
fault/stack/environment checks and whole unchanged-source accounting. No current
evaluator cases were accessed for this draft. No claimed admission delta until
fresh exact-source results exist. Complete Doe migration and production adoption
remain open after this owner.
