# M12 implementation handoff

Author candidate 2, 2026-10-23. Not independent acceptance, physical qualification,
production-default promotion, or complete compiler/runtime migration.

## Boundaries and source identity

The implementation began only after the independent input/oracle seal
`98a6656e0b3bf2bb2a6e74bb5977afabdab2ac2ac07c664468a8026dea43b281`
was released. Its private cases and oracle were not inspected. The installed
`CONTRACT_12.sha256` remains
`f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4`;
all nine entries still verify. Accepted M11 was
`f01effdf070edf2f25cf0f2016c995df1e4d3bab`. No earlier tests, receipts, public
headers, export map, CLI schema, pool limits or runtime source were changed.

One eleven-entry `NumericBuiltin` registry owns arity, shape family and explicit
extended-instruction identity. `sema_builtin.c` captures arguments in source order,
selects one operand type, validates mandatory constant clamp bounds and publishes
one runtime IR instruction. `builtin.c` projects existing typed constants and
assembles only the completed vector result. `numeric.c` retains the scalar
floating owner and adds bounded two-sum five-limb AI dot accumulation. Concrete
integer dot wraps modulo 32; the selected exact-final AI interpretation and its
Tint disagreement remain as disclosed in the frozen interpretation document.

The emitter consumes captured semantic IDs. Shape-reducing operations recover
operand types from the existing semantic owner. Vector min/max/clamp use scalar
compare/select; integer dot uses vector multiplication then extract/add reduction.
No generic dispatch framework, heap, second numeric environment or scratch pool
was added. Three duplicated vector-width queries now share `types.c`.

## Explicit diagnostic correction

A bounded syntax-only single-type template-call lookahead lets `dot<f32>(...)`
and nested single-type variants reach ordinary callee resolution. Their former
comparison parse error becomes a semantic call error. The same route rejects
explicitly templated shadowing user functions and nominal constructors. No
builtin-name recognizer, general template-expression grammar, multiargument
extension or new semantic admission was added. Comparison fallback and actual
TypeRef depth/pool charging remain. Read-only lookahead is bounded by
`3*(type_depth+1)+2` tokens per visit and reserves no syntax.

## Developer verification

The focused front door is:

```
make -C runtime/c/compiler test-numeric-builtins SPIRV_VAL=/path/to/spirv-val
```

It does not replace the independently sealed gate or rewrite historical negative
expectations. The following full developer evidence belongs to candidate 1; the
candidate-2 reruns and still-pending independent gates are distinguished below.
Author tests exercise:

- All 123 creation-time signatures and all 69 concrete runtime signatures through
  actual compiled/validated/decoded SPIR-V. The developer model's host math is one
  deterministic execution choice, not an independent floating-accuracy oracle.
- Exact complete output and source-order effects for reversed vec4 clamp and
  overflowing unsigned dot; snapshots across later mutations; swizzles; geometry;
  signed64 final range/cancellation and the exact `2^128` accumulator magnitude.
- Every concrete lowering row's exact result-ID and word counts, including the
  29-ID/155-word vec4 clamp and 8-ID/40-word integer vec4 dot.
- Invalid overloads, skipped mandatory clamp bounds, lexical/module shadowing,
  M11 varying dependencies, comparisons and nested type-depth exact/one-over cases.
- Four native adapters, six output modes, exact/short/stale spans, atomic outputs,
  and 768 concurrent mode iterations with numeric-error controls. Caller errno,
  rounding, exception flags and SSE subnormal modes are preserved in these checks.
- Fresh strict GCC and ASan/UBSan runs, retained scalar numeric controls and M11
  workgroup/API regressions, ordinary C11/C++17 shared-library consumers.

Observed unchanged layout: Core 552, Type 56, Constant 32, ConstantValue 16, IR 32,
TargetDeclaration 32, fixed workspace 146256. Registry records are 24 bytes.
The shared library exports exactly the retained seven functions and imports no
allocation or subprocess API. All four public headers and export map match M11.

The original author subtotal of 1440 bytes was incomplete: it omitted the
integer-attribute and type-reference members of the semantic recursive SCC.
It was not a valid recursive-family pass. Candidate 1's independent full graph
measured 1616 > 1536 for that SCC and 16424 > 16384 for all other frames.
Its immutable source snapshot and complete failure receipts remain unchanged.

## Candidate 2: complete stack correction

Only five production files differ from candidate 1: `src/builtin.c`,
`src/builtin.h`, `src/sema.h`, `src/sema_builtin.c`, and `src/sema_call.c`.
No developer or independent test, build flag, ceiling, record, pool, numerical
operation, public ABI or target lowering changed.

- Builtin lookup borrows the existing source `NameSpan`, rather than copying a
  72-byte `Name`. The prior identifier-length check and lookup precedence remain.
- Mandatory skipped clamp validation follows its two existing argument links,
  rather than retaining three syntax IDs across recursive expression calls.
- Call-context validation stays in the dispatcher, before argument evaluation;
  the typed builtin handler no longer retains an unused statement-context flag.
- Clamp-bound validation borrows two immutable `ConstantValue` addresses only
  until its return, instead of copying both values into another frame. The
  argument snapshots own those values throughout the call; no pointer is retained.

All 16 library units were rebuilt with fresh `.su` and `.ci` outputs under both
unchanged flag profiles. Complete graph discovery includes optimized clones,
address-proved local aliases, exact `.su`/`.ci` coverage and all seven recursive
families. The full semantic SCC is integer_attribute, postfix_value,
sema_builtin_value, sema_call_value, sema_expression and sema_type_reference.

Normal Makefile flags measure those six frames as 96/352/368/304/288/112 bytes,
SCC 1520 <= 1536, all-other-frame sum 16368 <= 16384, and combined fixed/job
bounds 205776/194544 <= 212992/196608. Exact independent PIC flags measure
96/320/368/304/288/112, SCC 1488, all-other sum 16264, and combined bounds
205672/194440. Other SCCs are parser expression/type 448, parser control 160,
semantic control 464, emitter 288, uniformity control 208 and uniformity
expression 112 normal / 128 PIC, each below its frozen ceiling. Both profiles
have ABI1 frame 146400 and largest other frame numeric_parse_literal 1344.
The normal/PIC graphs cover 162/159 optimized frames respectively. The combined
bounds conservatively add all non-wrapper frames once plus the unchanged phase
envelopes; they are not observed peaks and exclude external libc/libm, OS,
caller and sanitizer frames.

Candidate 2's unchanged nine-method developer builtin suite passes in 1.145
seconds, the six unchanged M11 workgroup methods pass in 0.260 seconds, and
fresh private exact-accumulator/layout controls pass. No sanitizer or native
stress run was repeated while the independent numeric gate was active.
Source-bound independent semantic/numerical, sanitizer, native and complete
ledger acceptance must be rerun for candidate 2; candidate 1 passes do not
qualify these changed bytes.

Five unchanged source-hashed selected witnesses compile and validate under
Vulkan 1.1: stable fluids seed, naive/shared matrix-vector, matmul-gemv and attention
decode. These are compile/validate results, not full numerical execution. Both
fluids advection witnesses still reject at excluded floor, at offsets 1017/994.
No new full-corpus, physical-device or runtime-adoption claim is made.

## Measured process and retained author evidence

Candidate 1's first strict build took 4.89 seconds. Its final nine-method developer suite
with all 69 exact target rows took 1.224 seconds; its sanitizer run took 2.627 seconds.
Read-only preparation began 13:49 UTC, implementation was released 13:58 UTC, the first
fresh native build passed 14:03 UTC and the first unchanged witness validations
passed 14:05 UTC. Owner/first-test discovery and a representative agent-edit control
were not separately timed; no productivity/performance improvement is claimed.

Relative to M11, 13 production/build files changed. The current compiler source,
headers and Makefile closure moves 7167→7564 physical lines (+397), with 682 new
separate developer-test lines. These are bounded compiler closure measurements,
not full-Doe like-for-like normalized LOC or the migration's 50% target. There is
no claim of line removal; duplicate decisions were consolidated instead.

Candidate-1 author evidence is retained at `/tmp/doe-m12-author-20261023`, including
first failures, final logs, `.su`/call graphs, exact witness results and the
root-relative `AUTHOR_SOURCE.sha256`. That manifest's SHA-256 is
`0c01ff844b4ad98ef88439bf5d0cdf4ac9ad8cd6f22143de52fdf77aa9381a7a`.
It binds production and four new developer-test files; this note is not included.
Its CLI SHA-256 is
`6153502fee41f04fe6788548761c9b996aa2a5d6475dc422c2d52934d8ed1d79`.
Candidate-2 evidence is retained at `/tmp/doe-m12-author-candidate2-20261023`.
Its root-relative 34-file `AUTHOR_SOURCE.sha256` has SHA-256
`73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`.
The `doe-compiler-c-final` SHA-256 is
`62d4b7ab942d40e744ff27af1312face9b781eec9c67f4dac07ea65f6695f9e9`.
`FINAL_BUILD_COMMANDS.json` records every fresh command and source identity;
`qualified-final/COMPLETE_STACK.json` and `audit-final/COMPLETE_STACK.json`
contain the two complete graph/frame/bound receipts. Intermediate author failures
are retained separately, not overwritten by these final receipts. Production and
test bytes were held before independent candidate-2 snapshotting; this handoff
note is outside the manifest.

Independent evaluation must build from its own exact source snapshot, not trust
these binaries or the author model. No commit or push was performed by this owner.

Component: experimental C compiler. Intent: preserved within frozen M12.
Boundary effects: none outside compiler; additional admitted numeric builtins and
one explicitly recorded malformed-call diagnostic correction within that owner.
