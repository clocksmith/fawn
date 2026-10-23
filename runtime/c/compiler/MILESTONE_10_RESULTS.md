# Accepted M10: typed aggregates, layout and reflection

The [frozen contract](MILESTONE_10.md) at `cbad7b0` passes independent acceptance
on final candidate v6. One shared pipeline now owns vectors, fixed arrays, nested
nominal structures, aggregate constants/construction, by-value calls, access paths,
checked resource layout, typed reflection and explicit legacy projection failures.
The additive ABI4 header does not change the three earlier public headers. The
unchanged binary-only CLI consumer uses this pipeline without requiring old metadata.

## Decisive evidence

- All 576 semantic/layout/byte-execution cases pass, with default Vulkan 1.1
  validation, plus five boundary probes and five nominal-lookup cases in six modes.
- All 839 earlier, 997 M8 and 380 M9 semantic checks pass through explicitly
  enumerated representation/admission adapters. All 12 M9 and 17 M8 emitted
  validator-valid mutants are detected. Original sources and first failures remain.
- The unchanged real consumer freshly emits, validates and independently executes
  **37 of 98 paths**, totaling 13,434 CPU invocations. The four new paths are three
  fluid shaders and `stress_many_bindings.wgsl`. The latter's independent oracle
  is a disclosed post-discovery supplement, not a presealed witness. The
  [ledger](../../../docs/c-migration/corpus-coverage-m10.json) preserves 61 explicit
  rejections (55 parsing, six semantic) and separate unrelated prebuilt observations.
- Native C/C++ source/archive/shared/prebuilt, ABI, capacity, ownership,
  concurrency, no-allocation, fenv/errno, fault and ASan/UBSan checks pass. The
  sealed native lane has 59 commands and 15 consumer runs. Retained coverage
  exercises 8,352 public exits in each archive/shared/sanitized route, 432 host
  profiles and 16 threads. CLI checks include 19 fault/rollback cases.
- Fresh portable source-only orchestration passes all 32 main stages without a
  prebuilt compiler. Separate portable native lanes preserve their own seals.
  Existing capacity/postdisclosure gates retain 31 and eight passing cases.
- The 65,533-member target-width probe validates with an explicitly raised
  validator member-resource limit after preserving the default-limit failure;
  65,534 members reject atomically. This is target-format evidence, not default
  resource or physical-device qualification. Saturated target-ID status is tested
  with a private boundary probe, not an execution claim for 2^32 target IDs.

## Costs and corrections

GCC/G++ 14.2, Linux x86-64, strict C11/FP flags and libm; no Zig participates in
compiler generation, build, link or execution. Fixed workspace grows from
119,408 to 140,056 bytes; ABI1 wrapper frame grows from 119,520 to 140,208.
Independent conservative combined library bounds are 197,160 fixed and 192,120
maximum-depth job bytes. Parser/semantic/emitter recursive-family allowances
measure 448/1,296/272. These are derived bounds, not observed stack peaks, and
exclude external libraries, OS and sanitizer frames. All frozen ceilings pass.

Independent evaluation found and preserved three real candidate defects:
valid excluded matrices had the wrong diagnostic category; saturated target-ID
exhaustion had the wrong failure category; nominal constructors named like
predeclared builtins/generators lost module lookup precedence. General fixes now
pass exact regressions. The first nominal fix also exceeded the semantic frame
ceiling (1,552 versus 1,536); one shared constructor-dispatch site restores the
bound without increasing it. Earlier candidates and that attempt remain retained.

Evaluator repairs are separate: a vec3 buffer supplied 44 rather than 48 bytes;
old scalar oracles assumed a flat uniform target type; new valid aggregate cases
need named positive checks; initial lookup probes had invalid output descriptors.
The [full accounting](independent_tests_m10/POSTDISCLOSURE_ACCOUNTING.md) preserves
their first outcomes and exact adapters. No malformed/type-invalid family was
broadly relaxed. Current Git-tracked prebuilt inventory differs from M9's on-disk
inventory; neither is candidate source coverage.

## Reproduction and qualification boundary

Use the [source-only instructions](independent_tests_m10/REPRODUCE_FINAL.md),
[accepted source manifest](independent_tests_m10/ACCEPTED_COMPILER_SOURCE.sha256),
and [independent verdict](independent_tests_m10/ACCEPTANCE_FINAL.json).
Candidate manifest SHA256:
`e818441e6c93feb04a7722fc1602f255cfd2e596d806604821aa7bbd178e1e11`.
Verdict SHA256:
`ddbb3ef36870045afb79d58cc5ab6da0c7af134d14f81c3c892fc6385819bfda`.
Acceptance navigation and imported tests do not alter those 30 bound inputs.

Workgroup synchronization/uniformity, other remaining language/target owners,
native TranslationInfo/cache/adoption, descriptor overlap/races, physical GPUs,
performance and whole-Doe migration remain unqualified. No production-default
owner is replaced by this standalone compiler acceptance.
