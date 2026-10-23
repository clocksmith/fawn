# AbstractInt dot: explicit interpretation, unresolved upstream convergence

Reviewed M12 owner interpretation: compute the exact mathematical dot
and reject only if its final AbstractInt value is outside signed64. This is a
deliberate, disclosed interpretation of dated WGSL text, NOT an upstream ruling
or a claim that every current implementation agrees. Parent selected this route
on2026-10-23 after the independent textual/CTS/Tint review. Design review confirms this disclosure, narrow cases and bounded accumulator
are coherent. Independent executable case/oracle sealing remains required before
implementation; this is not implementation acceptance.
No upstream contact, implementation edit, source build or execution occurred.

## Normative reasoning and its limit

Reference: [WGSL CRD21October2026](https://www.w3.org/TR/2026/CRD-WGSL-20261021/).
Section17.5.20 defines the mathematical dot and returns its component type.
Section6.2.1 restricts AI to signed64 and disallows overflow of an evaluated
abstract expression. Section15.7.7 explains the floating-point meaning of a
domain inherited from linear terms; its substitution uses floating operations.
It does not explicitly introduce AI multiplication/addition expression nodes.

The standing reviewer's textual inference therefore favors testing the final
mathematical result of the dot call rather than an implementation's invented
i64 product/summation temporaries. This inference is not a found working-group
decision. The spec does not separately spell out an AI dot reduction graph or
exact cancellation rule, so the design must not describe a debated choice as
universally settled WGSL conformance.

Concrete integer dot is separate and unambiguous here: §6.2.3 supplies modulo32
for concrete integer results, including const expressions. This draft does not
extend final-result-only handling to source `*`/`+` expressions, floating-point
builtins, conversions or other abstract numeric operations. Argument expressions
still have their ordinary individually checked type/range before dot is called.

## Primary-source implementation evidence, not normative override

The independent reviewer inspected these exact public upstream sources:

- [Tint pinned eval.cc](https://github.com/google/dawn/blob/e418298b91ae3fcb9dbb49a78a69a78df3d53904/src/tint/lang/core/constant/eval.cc#L717-L728)
  and [dot reduction](https://github.com/google/dawn/blob/e418298b91ae3fcb9dbb49a78a69a78df3d53904/src/tint/lang/core/constant/eval.cc#L828-L835)
  use checked products, so cancellation after an out-of-range product is
  rejected by that implementation path. This is a contrary implementation
  behavior, not proof that the spec requires those intermediates.
- [CTS pinned AI dot cache](https://github.com/gpuweb/cts/blob/6eb72d4a233f407d35836cefee17f6746f842752/src/webgpu/shader/execution/expression/call/builtin/dot.cache.ts#L23-L52)
  excludes inputs with out-of-range products and partial sums in any tested
  summation permutation. Undefined cache cases are omitted, not converted into
  validation-error expectations. The independent review also checked the same revision's undefined-case filter;
  the pinned dot-cache exclusions are the directly linked evidence here.
- The CTS dot validation file uses splatted inputs for overflow cases. Those
  cases cannot distinguish cancellation-to-range from final-result overflow.

Therefore neither the cache omission nor Tint behavior establishes a mandatory
creation error for the disputed cancellation. Conversely, no upstream source
located in this review proves an upstream-required exact zero. The acceptance
receipt must label these narrow expectations as the selected compiler
interpretation and record Tint disagreement separately from ordinary failures.
No wholesale CTS skip, weakening of other cases or universal parity claim.

## Distinguishing acceptance cases to seal

AI is not source-spellable; construct abstract vectors using unsuffixed values.
Use const declarations so default runtime materialization does not erase the
question. Exact integer expectations come from an independent arbitrary-integer
dot, not compiler scalar arithmetic or host signed overflow.

1. Out-of-range products, exact cancellation:
   `const r = dot(vec2(4294967296, 4294967296), vec2(4294967296, -4294967296));`
   Selected result0, not overflow. A diagnostic here is the disclosed Tint
   divergence case, not accepted as an implementation fallback.
2. In-range products, overflowing partial sum, final in range:
   `const r = dot(vec3(9223372036854775807, 1, -1), vec3(1, 1, 1));`
   Selected result9223372036854775807. Permute lanes; mathematical value remains.
3. Same products but final above range:
   `const r = dot(vec2(9223372036854775807, 1), vec2(1, 1));`
   Required creation error under selected interpretation and ordinary final
   signed64 range. This guards against accidental modular AI arithmetic.
4. Below-range counterpart: dot((-9223372036854775807-1,-1),(1,1)) is an error;
   dot((-9223372036854775807-1,0),(1,1)) is exactly INT64_MIN.
5. Exact cancellation with large signed products in four lanes:
   a=(INT64_MIN,INT64_MIN,INT64_MAX,INT64_MAX),
   b=(INT64_MAX,-INT64_MAX,INT64_MAX,-INT64_MAX) =>0.
6. All four products INT64_MIN*INT64_MIN =>2^128, creation error. The exact
   accumulator must represent that magnitude, not overflow at signed/unsigned128.
7. An explicitly written overflowing argument subexpression, such as
   `dot(vec2(4294967296 * 4294967296, 0), vec2(1, 1))`, remains a creation error.
   The rule does not bypass overflow in ordinary source expression nodes.
8. Concrete parallel control: dot(vec2<i32>(65536,65536),
   vec2<i32>(65536,-65536)) =>0 modulo32 in both const and runtime contexts.
9. A representable AI result outside i32 accepted as AI but rejected on inferred
   i32 materialization; explicit permitted wider-to-f32 conversion is checked by
   the existing conversion owner, not by changing the dot computation.

If review rejects this interpretation, revise this document and its independent
cases before implementation. Do not ship an edge-only UNSUPPORTED path while
claiming the full chosen overload family. Do not let older adapters use checked
product rejection while ABI4 uses final-result range checking. The selected
mechanism is the five-u32-limb accumulator in ../MILESTONE_12_LEDGER.md.

## Review status

Textual and upstream-source investigation complete; no decisive upstream
adjudication found. Parent route selection received. Final design review of disclosure, five-limb bound and distinguishing-case
sufficiency found no substantive blocker. Accepted M11 is bound by
../contracts/m12_source_inputs.json. The independently owned executable case seal,
implementation and numerical/backend qualification remain separate open gates.
