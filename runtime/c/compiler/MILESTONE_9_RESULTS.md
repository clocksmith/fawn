# Accepted M9: scalar functions and selected math

The [frozen contract](MILESTONE_9.md) at `1aa59cd` passes independent acceptance.
One shared parser/type/IR/emitter owns module signatures, immutable scalar formals,
real target calls/returns, left-to-right argument snapshots, full-body validation
and iterative call-cycle rejection. The closed builtin owner adds typed
min/max/clamp/exp/tanh/sqrt with independent constant accuracy checks and safe
runtime clamp lowering. Public headers/layouts and production selection are unchanged.

## Decisive evidence

- All 380 new cases (231 positive, 149 negative), 997 retained M8 cases and 839
  older expectations pass. New instruction trials cover 960 invocations, 1,805
  user-function calls and a 1,024-frame iterative interpreter chain.
- All ten unchanged witnesses pass. The actual unchanged consumer admits
  **33 of 98 paths / 31 unique sources**, each Vulkan 1.1 validated and independently
  instruction-executed, totaling 12,258 corpus invocations. The
  [ledger](../../../docs/c-migration/corpus-coverage-m9.json) keeps 65 explicit
  skips (64 parsing, one semantic), prebuilt observations and unrun hardware separate.
- Fifty-eight independent numerical enclosure controls and 52 handwritten target
  controls pass. All 12 new and 17 retained validator-valid emitted mutants are
  detected by execution. Host libm is not compared with itself as the oracle.
- Native C/C++ source archive/shared/provided-prebuilt, environment/errno,
  preflight/atomicity, no-allocation, fault and sanitizer gates pass. CLI: 174/174.
  Capacity: 41 compile statuses and 18 query edges. Existing fuzz, mutation and
  concurrent-call cases remain enabled.
- Actual SPIR-V format boundaries pass: 65,532 formals and 65,531 call arguments;
  each one-past rejects atomically. The latter emits 589,915 words and validates
  in 207.686 seconds with explicit `--max-function-args 65532`. The first default
  120-second observation remains inconclusive, not a pass or a language limit.

## Costs and preserved corrections

GCC/G++ 14.2, Linux x86-64, strict C11/FP flags and libm; no Zig in the compiler
build or execution. Fixed workspace grows from 76,512 to 119,408 bytes and ABI1
frame from 76,592 to 119,520, inside explicitly reviewed ceilings. Largest other
library frame is 1,344. The [independent combined audit](independent_tests_m9/stack-analysis.json)
bounds library frames at 159,976 / 40,568 / 149,064 bytes for ABI1 / v2 / maximum-depth
job respectively. These supersede rough developer path estimates, exclude
external library/OS/sanitizer frames and are not observed stack peaks.

No candidate implementation defect was found during independent evaluation.
Original failures remain: an evaluator adapter omitted the predeclared infinite
cases' StepLimit disposition, and old private-workspace assertions used obsolete
ceilings. Narrow derived adapters preserve all finite behavior and other checks.
Exactly five new old-rejection lifts are named in the
[detailed independent report](independent_tests_m9/EVALUATION_M9.md).
M9's right-operand signed-zero tie policy aligns constant and prior target max;
this is deterministic policy alignment, not automatically a WGSL conformance fix.
The developer's initial warning failure and recovered auxiliary probe provenance
remain in isolated evidence; neither substitutes for independent acceptance.

## Reproduction and limits

The [current portable source-only gate](independent_tests_m9/source/README_CURRENT.md)
uses a checkout and an explicitly supplied installed validator. Its source manifest,
original seals, exact named adapters and imported-test hashes remain separate.
The full raw source-bound receipts live in the isolated milestone9 evidence owner.
Candidate manifest SHA256:
`55af8003b0611795585af60ae5f9881ae53d14b1936f6c02223e75f879b85937`.
Independent [acceptance receipt](independent_tests_m9/ACCEPTANCE_M9.json) SHA256:
`55012f0e5781904fbb1a4993cee8e0298c8c012725416f28bb4f7dc2c9da418b`.
Only acceptance navigation changes after that candidate seal; implementation,
public headers, original tests and artifacts remain unchanged.

Aggregates, workgroup/collective semantics, wider targets, native TranslationInfo,
compiler/runtime adoption, physical backends and whole-Doe migration remain open.
This accepts the declared standalone compiler owner, not full WGSL, GPU accuracy,
performance parity or a production-default replacement.
