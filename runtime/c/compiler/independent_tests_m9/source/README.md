# Independent M9 source-only evaluator

This suite imports immutable accepted M7/M8 evaluator sources from the specified
Doe checkout. No candidate implementation, developer tests or candidate binary
was inspected/executed before the obligation and exact-source/oracle seals.
GCC/G++14.2, Python3.12 and an already installed spirv-val are sufficient; never
invoke Zig for this compiler path. No archived candidate tree, generated object,
private evidence folder, downloaded dependency, GPU or proprietary tool is needed.

## Reproduction

Place this directory beside the independently supplied native_capacity/source
package, or provide --native-source. Run from any directory:

    python3 run_acceptance_m9_reviewed.py --repo /path/to/doe \
      --source-root /path/to/doe/runtime/c/compiler \
      --validator /installed/path/spirv-val --evidence /fresh/output

--plan prints commands without candidate execution. --stages selects named
focused stages only, never full acceptance. --existing-build /explicit/build
reuses a separately source-attributed library/CLI. --prebuilt adds a separately
labeled supplied native library, never a substitute for source-build evidence.
Use --native-source /path/to/native_capacity/source if packages are not siblings.
The reviewed front door selects run_all_m9_reviewed.py, whose one documented
historical harness change is the explicitly approved fixed-workspace ceiling.

Builds occur only in the fresh evidence directory, using strict FP flags. The
three headers/export surface remain unchanged. Every target validation selects
--target-env vulkan1.1. Oversized instruction-format controls additionally raise
only spirv-val's --max-function-args resource ceiling, not language semantics.

## Frozen coverage and independent oracles

- 231 new positive sources and149 negative sources, including10 unchanged real
  witnesses and81 separately packed witness vectors; no source inlining or host
  interpretation supplies actual results.
- 839 immutable historical cases, including exactly2 newly lifted M4 rejections:
  trailing_function and unknown_call. All previous67 M8 lifts stay named.
- 997 immutable M8 cases, with exactly3 new named lifts:
  excluded_numeric_feature_4/5/6 (sqrt/exp/clamp). Existing documented M8 errata
  are applied unchanged, not broadened.
- The original full98 consumer expects33 fresh emissions after these10 new
  witnesses. Prebuilt modules, fresh emissions, skips/rejections, target checks,
  instruction execution and physical/adoption stages remain separately labeled.
- 52 handwritten target-valid controls, including actual nested/repeated typed
  function frames, side effects, formal order, returns and selected ExtInsts.
  Their initial parser-minimum mistake is retained in oracle-initial; corrected
  unary ExtInst minimum has passed before candidate inspection.
- 58 independent numerical controls. exp is enclosed by240-digit directed
  Decimal arithmetic,100 Taylor terms at |x|<=1/16 with a geometric tail bound,
  repeated squaring and reciprocal. tanh uses an interval exp identity. sqrt
  bounds are certified by exact Fraction squares. M8's Fraction nearest-even
  IEEE oracle converts enclosures. Neither constant expectation nor interpreter
  math calls host libm. AbstractFloat quality remains observable through a
  scaled binary64 residual before conversion to f32; a coarse f32 cast does
  not mask a low-precision binary64 result.
- 12 emitted-module mutation policies are sealed before candidate use. Their
  targets must independently validate and then fail actual execution; target
  rejection or an unknown instruction is never successful mutant detection.
- The separate native/capacity package covers41 exact generated capacity cases,
  format-word-count limits, caller-owned ABI boundaries, no allocation, C/C++
  archive/shared/prebuilt/sanitizer paths, FP/errno, CLI rollback and stack review.

Nonfinite/domain-runtime math has no promised portable physical payload.
Deterministic finite instruction-model expectations are not GPU-accuracy claims.
Each attention source's arithmetic order is preserved independently; different
summation orders are not required to produce equal outputs. Fixed public APIs
are compared only for sources fitting their unchanged capacity/reflection bounds.

## Evidence integrity and disk

PRE_CANDIDATE_SEAL.json binds sources, expectations, control receipts and139
immutable historical import files. Native source has its own original/addendum
seals. New result corrections, supplements and runner-only changes are recorded
separately and preserve first failures. Candidate source identity is bound around
each complete execution stage. During disk-constrained staged execution, completed
newly generated modules/receipts may be losslessly compressed after needed mutation
checks, retaining hashes and a compression manifest; old evidence is untouched.
No partial stage selection implies full acceptance. This campaign establishes no
physical Vulkan/Metal/D3D12 accuracy, runtime adoption, performance parity, browser
ownership or completion of the full Doe migration. MIT licensing and historical
oracle provenance are retained in LICENSE and IMPORTED_SOURCE.json.
