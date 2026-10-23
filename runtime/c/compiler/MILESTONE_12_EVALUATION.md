# M12 independent acceptance design

REVIEWED DESIGN ONLY. These are contract cases/oracle requirements, not author tests,
executed acceptance results or access to a current evaluator's cases. Freeze the
independent implementation/case hashes before candidate implementation and keep
them separately owned. Do not expose hidden evaluator inputs to the implementer.

## Decisive first check and hierarchy

First decisive compiler check after release to implement: fresh native source
build, then one dynamic vec4 clamp with reversed lower/upper lanes plus one
unsigned integer dot that overflows32-bit products. Decode and execute the
actual emitted SPIR-V, checking complete storage outputs, exact argument-effect
sentinels and typed result. These expose wrong clamp poison/ordering, signedness,
wrong return shape and missed integer modulo without requiring a full suite.

This first check is NOT acceptance. Complete gates below preserve accepted M10
and accepted M11 successes, source-bound native/API checks and unchanged corpus
accounting. Whole-application numerical execution, physical GPU qualification,
runtime metadata adoption and other backends are separate gates.

## Full overload and stage matrix

Resolved signature coverage: min/max/clamp each5 leaves*4 shapes=60 total;
exp/tanh/sqrt each2 leaves*4 shapes=24 total; dot5 leaves*3 widths=15;
length/distance each2 leaves*4 shapes=16; normalize2 leaves*3 widths=6;
cross2 leaves=2. Total123 legal signatures in the selected type envelope.
All123 get creation-time coverage. The69 concrete signatures also get genuine
runtime inputs (storage/formals or let snapshots), not constant-folded aliases.
These are planned matrix counts, not executed-case counts or admissions.

Each family needs module const, local const, a constant nested in a runtime
argument, helper result, vector member/array element and swizzle cases. Test all
three widths, all argument positions, and nonuniform lanes so one splat cannot
pass as full vector coverage. Module forward references and dependencies must
use the same result/type behavior as local const and must reject real cycles.

Add ranked-conversion controls: AI->AF for floating families; AI/AF+f32 same-
shape common overload; AI+i32 and AI+u32 in numeric families; an out-of-range
abstract lane failing materialization; a representable whole AI result that
does not fit default i32; explicit constructor versus implicit materialization;
mixed concrete i32/u32/f32 rejects. A type-only visit must not mutate runtime
effects or allocate losing speculative overload records.

Negative signature suite: scalar dot/normalize, vec2/vec4 cross, bool arguments,
arrays/structs, unequal widths, scalar+vector calls for every multiargument
family, concrete integer exp/tanh/sqrt/length/distance/normalize/cross, wrong
arity0..4 as applicable, explicit generic arguments, bare function-call statements. Distinguish phony assignment (a valid WGSL use
but an existing unadmitted statement feature), not a builtin must-use error.
Repeat errors in unused helpers/dead code/skipped RHS. Excluded f16/matrix/floor
remain explicitly unsupported, not accidentally accepted via generic registry.

## Namespace, evaluation and uniformity cases

- For every new name, test user function, nominal constructor and module value
  shadowing. Local/formal/input names produce ordinary lexical-value call errors.
  Nested scope exit restores the original binding. No string-based dot/barrier
  treatment survives semantic resolution.
- Helper arguments increment a storage counter and write unique sentinels.
  Two/three arguments must execute once in source order. Earlier vector loads
  are captured before a later argument mutates that resource. Repeat with vec3
  padding, swizzles, array elements and nested builtin/user-call arguments.
- Robust OOB argument loads return their own typed zero, OOB stores discard,
  and a zero returned by an inner OOB index may be a valid outer index. Preserve
  untouched guards beyond each allocation; no lane expansion creates pointers.
- Skipped runtime RHS has no argument side effects. Nonetheless illegal names,
  overloads, cycles and mandatory const clamp bounds are checked. Test bad lower
  vs upper in each lane, both const bounds with runtime value, bounds hidden in
  module/local consts, and bounds constructors in skipped expressions.
- Runtime reversed integer clamp deterministically yields the upper bound in
  each reversed lane; floats use the selected min(max()) behavior within WGSL
  latitude. Mixed const/runtime bound stages are runtime, not mandatory errors.
- M11 barrier cases: dot(varying_input,zero_vector), length/distance of varying
  input, clamp(varying_value,const,const) and exp/sqrt/normalize/cross dependence
  must not be declared uniform from numeric simplification. Put varying values
  in each argument position. A varying argument on a skipped numeric RHS still
  contributes to the source graph. User function/constructor shadowing must use
  their actual summaries, not builtin identity. Include positive uniform source
  controls so always-reject is detected. Charge the existing budget exactly.

## Integer and exact finite-value oracle

The independent oracle parses input bit patterns into Python integers/Fraction
or an equivalently independently reviewed bounded integer model. No signed C
overflow or calls into candidate helpers. Concrete dot computes unbounded
sum(a_i*b_i), reduces once modulo2^32, and interprets i32 from the resulting bits.
Include INT32_MIN*INT32_MIN, UINT32_MAX products, cross-lane cancellation, product-
only overflow, addition-only overflow, and result zero with every nonzero lane.
Widths2/3/4 and lane permutations must all be covered. Five-limb AI internals
are not used by the oracle; use arbitrary integers and signed64 final checks.

contracts/m12_numeric_interpretation.md supplies the mandatory narrow interpretation/divergence
cases. Keep explicit-source arithmetic overflow controls separate so a shared
unchecked add/multiply implementation cannot accidentally pass them.

Mathematical geometry controls include dot((1,2,3),(4,5,6))=32;
length((3,4))=5; distance((-1,2),(2,6))=5; scalar length(-7)=7;
scalar distance(-2,3)=5; cross((1,0,0),(0,1,0))=(0,0,1) and reverse=(0,0,-1);
cross((1,2,3),(4,5,6))=(-3,6,-3). For length/distance/normalize these are exact mathematical centers, NOT
mandatory target bit patterns: carry the inherited sqrt/division accuracy.
Axis normalization checks direction and the permitted unit-component interval;
normalize((3,4)) uses independently enclosed 3/5 and 4/5, not decimal equality.
Use nonzero mixed-sign lanes for component-wise min/max/clamp to discriminate
unsigned/signed compare and dropped/reordered lanes.

## Two different floating oracles, never host-libm self-comparison

1. Chosen const-policy oracle: software binary32/binary64 round-to-nearest-even
   after each explicit primitive in the frozen graph. Parse exact input bits as
   signed integer*power-of-two. Integer division/remainder and tie bits compute
   adjacent endpoints/rounded bits. Check finite range per actual primitive.
   Transcendental/sqrt results use the mathematical enclosures below plus the
   documented acceptable ULP bound; do not assume a particular host libm last
   bit unless the contract separately proves it. This detects f32 folding at
   binary64, premature concrete materialization, illegal contraction, wrong
   constant stage and missing overflow errors.
2. WGSL runtime-accuracy oracle: separately construct the allowed finite result
   set/interval from the dated §15.7 accuracy rules. Addition/multiply may choose
   either adjacent representable value; enumerate relevant N<=4 reduction trees,
   rounding branches and permitted fused alternatives, or a proven conservative
   outward enclosure. A driver's/independent model's one left-fold result is
   not the universal expected answer. For exact small/orthogonal controls, use
   the strict result to retain discriminatory power.

For f32 exp, apply the dated relative-ULP bound as a rational function of |x|;
for sqrt, carry inverseSqrt's2ULP bound and subsequent division allowance rather
than silently demanding correctly rounded sqrt. For tanh, account for the worse
of the absolute1e-5 allowance and inherited sinh/cosh expression, whose exp-based
error must be propagated separately. For dot/length/distance/normalize/cross,
propagate the inherited arithmetic expression and explicit division range.
AF approximate builtin accuracy is not the same normative f32 guarantee: report
the chosen qualified host envelope independently, with high-precision quality
controls, without inventing a WGSL AF ULP mandate.

Mathematical enclosures require only bounded stdlib integer/Fraction/Decimal:
- sqrt of a nonnegative rational: choose fixed binary precision (e.g.192bits),
  integer-square bracketing/isqrt and exact comparisons to produce L<=sqrt(x)<=U;
  increase precision until the f32 decision is unambiguous, with a declared
  oracle work ceiling and explicit inconclusive outcome, never a false pass.
- exp on bounded rational inputs: choose k so0<=r=|x|/2^k<=1/8; use Taylor terms
  through m with next-term geometric bound next/(1-r/(m+2)); interval-square k
  times; use reciprocal endpoints for negative x. Fixed m40 and |x|<=8 give a
  small useful calibration set without importing libm. Beyond that range use
  separately bounded precision/range policy, not an unbounded oracle loop.
- tanh on bounded inputs: monotone interval transform(E-1)/(E+1) from an exp(2x)
  enclosure, odd symmetry for negative inputs. Accuracy latitude is applied
  outside this mathematical enclosure, not confused with Taylor truncation.
- Division and normalization: exact rational quotient plus outward rounding and
  dated denominator-range constraints. Geometric interval input dependencies
  must not be treated as independent where that creates false exclusions.

Zero sign, subnormals/flush-to-zero, near overflow, negative sqrt, zero normalize,
NaN/Inf runtime inputs and infinities introduced during evaluation form a
separately labelled latitude/error suite. Do not accept ordinary finite-domain
wrong answers merely because the edge suite permits indeterminate values. A
wide interval that contains a deliberately wrong-lane mutation is not a useful
acceptance oracle. Report vacuous/inconclusive cases explicitly.

## Actual target execution and mutation controls

Independently decode the produced SPIR-V words and types. Validate with the
existing already-installed validator under Vulkan1.1, recording hash/version.
Execution must read actual instructions and complete memory outputs, not source
spelling or the compiler's own IR. Implement OpDot and selected GLSL operations
in the independent model with the integer/rational oracle; do not route both
compiler and evaluator through the same host exp/tanh/sqrt and call agreement
independence. The model may pick a documented deterministic allowed result;
the separate WGSL oracle must still accept/check that result.

Required killed mutations: wrong scalar/vector return TypeId, signed compare for
u32, swapped/minimized clamp ordering, direct poison-prone target Clamp,
dropped/swapped dot lane, missing modulo, missing AI final range, checked AI
product contrary to chosen interpretation, reversed cross orientation, duplicate
argument execution, reload after later effect, omitted const-bound check,
dropped varying-argument dependency, enum-order import omission, and one target
ID/word undercount. Mutations must be independently introduced into output/model
or separately built throwaway candidates, never kept in production sources or
allowed to rewrite frozen cases after failure.

## Pools, native boundaries and regressions

For every affected pool E/Q/K/L/I/O/D/V/W, create an actual exact-capacity success
and one-short failure whose first exhausted pool is isolated; include nested
builtin arguments and mandatory skipped const bounds. Preserve T/S/R/X controls
and their previous owners. Verify fixed, v2, job and typed APIs with identical
source semantics, explicitly capacity-adjusting only where the old public
adapter actually lacks storage or aggregate reflection. WORDS/REFLECTION/MEASURE
all perform emission and fail with no partial output at the same private limits.

Check exact-sized/disjoint/aligned workspace spans, stale/misaligned/overlapping
inputs, output canaries, output required counts and source diagnostics; header
sizes/enums/seven exports unchanged; no malloc/new/subprocess/fallback; ordinary
C11 and C++ consumers; strict FP/fenv/MXCSR/errno restoration on every new error
exit; simultaneous distinct workspaces. Sanitizer builds and complete .su/call-
graph bounds must bind actual source, not the planning ledger. Keep individual,
recursive-SCC and combined-stack ceilings independent.

Historical original tests/receipts stay unchanged. Derived adapters name each
newly admitted vector builtin negative and preserve every other assertion.
Exact scalar-only prior sources retain their behavior and reasonable target
footprint; no mandatory extra import/Q/K/L allocation. A zero-new-admissions
result can be honest, but cannot replace the selected semantic obligations.

## Unchanged-source and cleanup evidence

First unchanged whole-source candidates are stable_fluids_seed and naive-swizzle
matrix-vector WGSL. Shared matrix-vector/matmul-gemv/attention-decode depend on
accepted M11 identity, full reduction and independent output tolerances. Bind
each file hash from existing WITNESSES.json; record compile/validate/execute,
first-error and full-source secondary blockers separately. The same98 corpus
must state new emitted, validated, executed, validation-only and rejected counts
without turning a first-error disappearance into a full admission claim. Keep
the floor/texture/graphics-blocked witnesses as explicit remaining exclusions.

Report measured owner/first-test discovery time, files read/touched, production
versus test/doc line changes, eliminated duplicate decisions, focused/full gates
run, build/test latency and blocked time. This draft has no measured performance,
line-removal, allocation-growth, stack-peak or regression-escape improvement.
No broad rerun beyond the named acceptance gate is requested for prose alone.

## Retained planning arithmetic

contracts/m12_planning_cases.json contains 24 concrete contract examples and 21 mathematical
enclosure calibrations (seven each for exp, tanh and sqrt). It was generated
using only standard-library arbitrary integers/Fraction, directed Decimal
printing at75 digits, and integer isqrt. Exp uses m40 and reduction to |r|<=1/8;
sqrt uses192 binary fraction bits. No candidate code, shader, build or host-libm
transcendental was called. Those data are public-to-author planning examples,
not the later independently sealed acceptance suite and not an executed pass.
The 123/69 exhaustive signature matrix is specified above, not claimed to be
expanded into these24 examples. Narrow AI disagreement cases are labelled.
