# Accepted M8: scalar numerics and constant ownership

The [frozen contract](MILESTONE_8.md) at `0e1e201` passes independent acceptance
without a candidate implementation repair during that campaign. Public ABI1,
v2 and job headers, layouts, exported symbols and JSON schemas are unchanged.
Production compiler selection is unchanged.

## Responsibility and real consumer

One lexer/parser/type/IR/emitter pipeline now owns AbstractInt/AbstractFloat and
concrete i32/u32/f32/bool stages, numeric literals/operators, conversions/bitcast,
module/local constants, iterative dependency/cycle resolution and constant
attributes. Let/var reads remain runtime values. Safe integer division/remainder,
shift and float-to-integer operands are prepared before target instructions, not
repaired by selecting away an already invalid result. Signed indices retain
independent per-resource robustness. Fixed APIs retain actual memory/reflection
limits, not permanent copies of experimental grammar exclusions.

The qualified Linux x86-64 host evaluator saves and restores caller floating
rounding, flags, traps and subnormal modes before output publication, including
failure paths. The library has no hidden allocation or retained caller pointer.
Its explicit libm/fenv dependency and strict FP build flags are not a universal
host or physical-device numerical guarantee. The dated WGSL interpretation and
hex-literal ambiguity remain explicit in the frozen contract.

All six unchanged numeric witnesses pass independent emitted-instruction
execution. The actual unchanged emitter consumer now admits 23 of 98 paths,
containing 21 unique sources, with 11,626 CPU instruction invocations. All 23
validate explicitly for Vulkan 1.1. The [source-bound ledger](../../../docs/c-migration/corpus-coverage-m8.json)
preserves 75 explicit rejections: 73 parser and two semantic failures. First
failures are not complete dependency analyses. The separate prebuilt audit has
35 valid of 36 observed modules; the pre-existing invalid Fragment Position
module is not candidate output. EMIT-SKIP never counts as acceptance.

## Independent evidence and preserved failures

- 997 resolved semantic cases: 826 positive and 171 negative, with 2,999 CPU
  invocations. Original preinspection cases and expected bytes are retained.
- All 839 historical cases pass, including 67 named newly valid admissions,
  with 21,528 instruction invocations. Historical suites are byte unchanged.
- Thirty-one capacity checks and eight separately labelled post-disclosure
  overload regressions pass. Word exhaustion and actual fixed-IR exhaustion are
  distinct atomic failures; source size never authorizes deeper C recursion.
- Thirty-six handwritten numeric safety controls and 17 valid emitted-module
  mutants are detected by actual instruction execution, not validator rejection.
  Controls include unsafe division/conversion before select, signedness, shifts,
  bitcast, short circuit, predecessor values, robustness and contraction.
- C/C++ source archive, source shared library, provided prebuilt and sanitized
  native paths each exercise 2,368 caller-environment calls. Sixty injected host
  failures and 174 CLI environment/acquisition/rollback checks pass. Headers,
  exports, allocator exclusion and output publication ordering are checked.
- ASan/UBSan and concurrent distinct-environment tests pass in their exercised
  scope. LeakSanitizer is not qualified in this environment.

Before handoff the author found a real overload edge: an abstract integer shifted
by a runtime count must concretize through the concrete i32 overload rather than
remain abstract. The first INPUT_ERROR receipt and exact repair checks survive.
The independent evaluator found no candidate defect. Two supposedly positive
float fixtures were outside the exact finite range and correctly rejected;
seven rejection-status assumptions were too narrow. An initial IR-pressure
fixture instead hit the separate word budget. Each original case/result and its
precise correction is retained in [evaluator errata](independent_tests_m8/EVALUATOR_ERRATA_M8.md).
The corrected true-IR stress and emitted mutations are labelled postinspection,
not retroactively described as frozen tests.

## Measured cost and reproducibility

GCC/G++ 14.2, Linux x86-64, C11 `-O2 -fno-fast-math -ffp-contract=off
-frounding-math`, linked with `-lm`; no Zig generation, compilation, linking or
execution in this compiler path.

- Fixed workspace: 73,400 to 76,512 bytes; ABI1 frame: 73,488 to 76,592 bytes.
  Largest other library frame: 1,344 bytes. Frozen ceilings remain satisfied.
- Conservative recursive-core sums are 35,488 / 88,960 / 22,224 bytes for
  parsing / semantics / emission, excluding adapters/callers. These deliberately
  broad derived estimates are not measured process high-water marks or a total
  stack reduction. Job 64/32 and fixed 18/4 depth/count envelopes remain.
- Independent CLI / shared library: 123,160 / 113,280 bytes. Private C/H source
  is 4,092 lines versus M7's 2,967, reflecting more capability, not language gain.
- One recorded retained-suite run took 3.595 seconds; the unchanged consumer
  stage took 2.972 seconds. These are local check-cost observations, not matched
  compilation or runtime performance comparisons.

Candidate manifest SHA256:
`a877ba32e0328f0e98d9c6d62f982dc706b657d7a73c1280a5034b0b9961ebba`.
Independent verdict SHA256:
`dc4ac6b68a291f36f6c064de5c353df446940361b53fc8d4abfdb9255dac7cc0`.
Independent CLI SHA256:
`71168dcc97dbf8cc35941dc4ccc5760d654af8eb98350fc5ac79b6e12c54d1a0`.
Independent library SHA256:
`86b45354823c13ea1831a40a06e7a3bd58745b6cf94ff2a76a37eabd7bccc151`.
The [portable source-only suite](independent_tests_m8/README.md) reproduces the
checks from a checkout and explicitly supplied existing validator. Source,
first-failure and command receipts remain in the isolated `milestone8` and
`milestone8-implementation` evidence owners.
Final-location verification checks all 51 portable payload hashes and 33 candidate
entries, ordinary imports/full command plan, eight focused overload cases and a
public-header/prebuilt C++ job smoke without repeating the completed campaign.

Helpers, wider builtins, aggregates, workgroup/collective semantics, other targets,
runtime/compiler adoption and physical backends remain open. This accepts a
reusable scalar owner, not complete WGSL or whole-Doe migration.
