# Accepted M12: typed scalar and vector numeric builtins

The [frozen contract](MILESTONE_12.md) passes independent acceptance on candidate 2.
One shared typed pipeline owns the admitted overloads of min, max, clamp, exp,
tanh, sqrt, dot, length, distance, normalize and cross: 123 creation-time and
69 concrete runtime signatures. Four public headers, seven exports, caller-owned
storage and output-publication rules remain unchanged. This is not complete WGSL,
native runtime adoption, physical GPU qualification or a production-default switch.

## Decisive evidence

- The unchanged CLI consumer freshly **emits and validates 67 of 98 paths;
  48 paths have independent execution evidence and 19 are validation-only**.
  Another 31 paths reject explicitly. The [source ledger](../../../docs/c-migration/corpus-coverage-m12.json)
  records every result; prebuilt artifacts are not new source admissions.
- Five unchanged full sources execute 40 complete dispatches under four schedules
  and two poison states, totaling 22,247,472 target instructions. They cover fluids
  seed, naive/shared matrix-vector, GEMV with its scalar tail, and attention decode.
  Candidate 2 preserves all those candidate-1 target bytes exactly. Its other
  43 execution-qualified corpus targets match accepted M11. These are explicitly
  carried-forward target proofs, not 48 newly executed candidate-2 sources.
- The effective independent semantic set contains 966 distinct sources:
  591 successful compilations, 375 rejections, 577 fresh instruction executions
  and 14 validation-only successes. This combines 931 primary cases, 31 numeric
  supplements and four separately sealed post-observation controls; repeated
  ABI/environment calls and historical cases are not added to that source count.
- All 576 retained M10 and 120 M11 source controls pass fresh compile/validation.
  Only the exact former vector_math/vector_dot rejections are intentional M10
  admissions. The 24 retained M11 execution targets are byte-identical; their
  expensive execution evidence is carried forward, not rerun or shortened.
- Fresh native source/archive/shared C11/C++17 consumption, 74 retained commands,
  200 numeric commands, 32 host-environment profiles, eight concurrent workspaces,
  capacity/publication checks and 128 ASan/UBSan commands pass. All 448 normal
  native and 224 sanitizer-emitted targets independently validate and execute.
  The qualified host is Linux x86-64, GCC/G++ 14.2. No Zig builds or runs this C path.

## One owner, real correction, unchanged limits

An eleven-entry typed registry owns arity, shape and target identity. Arguments
are evaluated once in source order. Constant and runtime stages retain distinct
rules; skipped source still receives required clamp-bound and uniformity checks.
Typed scalar primitives and aggregate constants remain their existing owners.
Vector width queries now share one type-owner operation; no macro framework,
private heap, second environment or scratch pool was introduced.

Candidate 1 genuinely failed the complete stack gate: its six-function semantic
recursive group was 1,616 > 1,536 bytes, and the sum of other frames was
16,424 > 16,384. The author's earlier four-function subtotal omitted two members.
The preserved failure is not waived. Candidate 2 borrows existing source spans
and immutable argument values and follows existing syntax links instead of
copying records/retaining duplicate state. No ceiling, ABI, pool, flag or target
lowering changed. Fresh complete call-graph audits and all affected independent
semantic/native/sanitizer gates qualify the changed bytes.

Normal Makefile flags now measure semantic group 1,520, other-frame sum 16,368,
and conservative combined fixed/job bounds 205,776/194,544 bytes. The strict PIC
profile measures 1,488, 16,264 and 205,672/194,440. Frozen ceilings remain
1,536, 16,384 and 212,992/196,608 respectively. The normal profile has only
16 bytes of margin in both first bounds: future edits require complete accounting.
Fixed workspace remains 146,256 and ABI1 frame is 146,400. These are derived
library bounds, not observed stack peaks; external libc/libm, OS, caller and
sanitizer frames are excluded.

## Numerical interpretation and preserved evaluator failures

The [numeric interpretation](contracts/m12_numeric_interpretation.md) explicitly
selects exact-final AbstractInt dot accumulation, with a five-limb positive/negative
sum that includes magnitude 2^128. Current Tint checks intermediate products and
can reject a cancellation case this owner accepts; official CTS does not settle
that counterexample. This is a disclosed interpretation, not a claim of settled
upstream conformance. Runtime accuracy intervals, finite domains and raw artifact
behavior remain separate from universal physical floating-point guarantees.

Two exact evaluator expectations were corrected with dated specification and
accepted-M11 controls: a concrete-only f32 constructor must reject the oversized
abstract integer input, and a purported runtime nonfinite test actually const-
evaluated a nonfinite bitcast. Original cases/seals/failures remain unchanged;
separate valid controls are labeled post-observation. Supplemental lowering and
capacity probes also retain their comparison-polarity/status-label harness
corrections. No broad malformed/type-invalid family was relaxed.

## Reproduction, source and remaining gates

The [portable source-only gate](independent_tests_m12/PORTABLE_USAGE.md) records
fresh build, semantic, numeric, corpus, native and sanitizer commands. Its exact
[acceptance receipt](independent_tests_m12/receipts/ACCEPTANCE.json) has SHA-256
`51ef417b1e339c1077a08336ed78514ad6ae5965c99fdf4d26cd5a9833a1749e`.
Candidate source-manifest SHA-256 is
`73b51c7355912608f71f3f19c44b7f57f17ef3f8b05be9fbf05bccd5331aaaf4`;
contract-seal SHA-256 is
`f3fef320194d5a81fa67d1cd394bd675c287e25b5ea492260758929bd1f274a4`.
The [author handoff](MILESTONE_12_IMPLEMENTATION.md) remains a historical author
receipt, including its then-pending independent gates, not the acceptance verdict.

The bounded compiler implementation/header/build/export closure grows from
7,167 to 7,564 physical lines (+397); new developer tests add 682 separate lines.
This is a partial closure measurement, not the full-Doe fair 50% comparison.
No speed, productivity, readability superiority or universal C advantage follows.

Multiple entries and runtime resource/workgroup metadata, matrices, pointers,
aliases, overrides, remaining builtins/collectives, textures/graphics, other
backends and actual runtime adoption remain open. Nineteen corpus emissions lack
execution qualification. No physical GPU, TSan, complete compiler or whole-Doe
migration claim follows from this acceptance.
