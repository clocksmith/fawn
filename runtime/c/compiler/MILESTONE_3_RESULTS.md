# Existing emitter-consumer integration: accepted scope

The frozen [M3 contract](MILESTONE_3.md) changes only CLI reflection admission.
The existing `bench/gates/spirv_val_gate.py:emit_spirv_for_wgsl` now consumes the
C executable unchanged using `--shader-path INPUT --out OUTPUT`. It also retains
explicit two-output reflection mode. Production Zig and all library semantics,
source files, public ABI and the existing consumer remain unchanged.

## Artifact and failure identity

- Accepted CLI SHA-256: `82bfb2e932499bb1155ac86e4b8ea53df62b63f0d1c92209fa87cbf268ff73cb`.
- Unchanged M2 library SHA-256: `38281d1e2c2842a925915ad6ab6dea7ddd332f86c381e43561b68b99080f63cc`.
- Fresh strict GCC builds and separately copied prebuilt artifacts are
  byte-identical, but tested as distinct source-build and prebuilt stages.
- The retained M2 CLI fails the actual consumer protocol because it requires
  reflection. First M3 candidate `086185d6...` fixed that mismatch but accepted
  `--reflection` as a missing `--out` value and created that unintended filename.
  The developer found it; independent evaluation reproduced it. Candidate 2
  rejects recognized options in path-value positions. Literal option-named files
  remain usable through `./` paths. Original failures are retained, not relabelled.

## Acceptance and exact limits

Independent CLI checks pass: 763 source-built, 119 copied-prebuilt, 170
wrapped-fault and 170 ASan/UBSan cases. Retained M2 passes 2,996 API, 2,932
capacity and 658 CLI checks plus 23 oracle controls and its native/sanitizer
stages. Counts describe separate check sets, not unique defects or source cases.

Independent tests exercise source-built and copied-prebuilt artifacts, both
reflection modes, the actual consumer on both residual source paths and the
neighbor-sum fixture, no-clobber/source preservation, malformed options and
injected write/close/link failures. The unchanged M2 suite still passes; its
trailing `--reflection` case is a missing value, not the newly admitted omission.
ASan/UBSan instrument the CLI and fault helpers; LeakSanitizer is disabled in this
environment. Developer API/ABI, ordinary C/C++ consumers and library sanitizers
also pass. The [source-only suite](independent_tests_m3/README.md) retains the
independent tests and their exact commands without a Zig dependency.

A separate candidate-2 unique-output audit covers 98 discovery paths, comprising
91 unique source hashes. Exactly two paths compile, both copies of one residual
source; 96 paths / 90 unique sources reject. First failures are 87 lexical and
nine parser rejections. Their semantic validation and emission stages are not
reached, not failed target validations. Both emitted modules pass explicit
Vulkan 1.1 validation and 274 independent instruction trials. Neighbor-sum is
independently exercised outside this discovery denominator.

The single full, unchanged discovery-gate observation belongs to rejected
candidate 1, not candidate 2. It printed PASS despite two fresh emissions and 96
EMIT-SKIP records, and separately validated 36 unrelated prebuilt modules. Its
bare validator invocation is not explicit Vulkan 1.1 qualification. Duplicate
basenames can share output names. Neither its successful exit nor those prebuilt
modules establish source coverage. The candidate-2 audit avoids filename
collisions without changing the real consumer.

The source-bound per-stage ledger and raw receipts live in the isolated evidence
workspace `doe-c-migration-evidence-20261023/milestone3/evaluator/`. The ledger
separates lexing, parsing, semantic validation, emission, target validation,
instruction execution and unrun backend qualification. It records first failures,
not every feature missing behind them.

## Measured change and remaining gates

CLI source grows by 16 net lines; the shipped CLI remains 51,192 bytes and the
library 49,736 bytes with this toolchain. No speed, lifetime cost or language-win
claim is made. This is a real compiler-component consumer integration, not MSL
benchmark, native WebGPU runtime, source-to-GPU application, subgroup, physical
backend or complete compiler acceptance. Zero production owners are replaced.
