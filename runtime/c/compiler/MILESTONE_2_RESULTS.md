# Structured neighbor sum: accepted compiler increment

Accepted on 2026-10-23 against [the frozen second contract](MILESTONE_2.md),
committed before implementation at `2b999ca`. This extends private compiler
control-flow/value ownership and its tests. The public version-1 header, API
publication owner and CLI owner are unchanged. No production owner is replaced.

The exact unchanged `sample-prefix-sum.wgsl` source compiles. Its behavior is
adjacent neighbor sums, not a prefix scan. Typed IR now expresses independent
indices, unsigned comparisons, structured selections and return. Validation
covers dead source; emission preserves source order, guards each access and
propagates live-arm termination, including unreachable merges after both arms
return. No source-template recognizer or runtime interpreter is introduced.

## Independent acceptance

The [self-contained source-only suite](independent_tests_m2/README.md) preserves
the historical M1 suite and receipts. Its first black-box freeze retains all
117 M1 positives and 169 still-invalid M1 cases, with five exact documented
rejection lifts. With new cases, 164 admitted and 240 rejected fixtures were
frozen before source inspection. The five additional access-trace cases in the
second freeze were also added before source inspection.

- Initial black-box API acceptance: 725/725 checks.
- Expanded capacity/concurrency API checks: 2,996/2,996; this includes the
  earlier cases and is not an additional disjoint 2,996-case corpus.
- CLI: 658/658 checks. All 328 admitted API/CLI modules pass external
  SPIRV-Tools validation for Vulkan 1.1.
- API and CLI each pass 16,266 instruction-level invocation trials, for 32,532
  total, using hand-authored buffer results and access traces. Untaken-arm
  accesses and OOB pointer formation are checked, not merely final outputs.
- Eleven retained and twelve new instruction-oracle controls pass, including
  deliberate wrong branch/return/arithmetic/Phi/bounds behavior. Unknown
  instructions remain oracle limitations, not candidate-invalid results.
- Source-built C/C++ consumers, protected-page/byte-boundary probes, mixed
  parallel valid/invalid calls, exports/dependencies and seven injected CLI
  publication failures pass. No Zig dependency is used.
- ASan/UBSan instrument the compiler and callers: 239,540 frozen-corpus calls
  plus 100,000 deterministic malformed/mutated cases pass. Retained M1 native
  checks also run. LeakSanitizer remains explicitly disabled.
- After black-box acceptance, an independently derived worst-capacity probe
  exercises 209 typed instructions and passes 2,932 capacity/semantic checks.
  This post-review probe is separate from the pre-inspection fixture count.
- The portable runner passes all 39 stages from its committed location.

The developer gate separately retains earlier API/constant/parallel/native/CLI
checks and adds eleven semantic test methods, 16/17-statement and 4/5-nesting
boundaries, original unreachable diagnostics, guard pages, mutations and 1,024
structured parallel calls. Its eleven new grammar examples all reject when
replayed against the preserved M1 executable. Developer and independent case
sets are separate; their rejection-lift counts need not match.

## Artifact identities and measured costs

Final Linux x86-64 GCC 14.2.0 artifacts:

- CLI: `95e13cca631da7cfe047d084a18ea84fe68cec30883cb8eb954fb1271c923044`.
- Library: `38281d1e2c2842a925915ad6ab6dea7ddd332f86c381e43561b68b99080f63cc`.
- Real shader: `a8a642a54aaca908b707d20666566db8db62dbb5c9bfd518fbe98925da22c416`.

Both milestone contracts and the public header retain their frozen hashes.
Commands/flags are in the Makefile and portable runner; generated receipts bind
their exact invocations and artifacts. Source notices/provenance remain intact.

GCC `-O2` reports an 88,672-byte static compile frame, versus 68,736 bytes at M1,
an increase of 19,936 bytes. Callees and caller buffers are additional. The
measured parser/lowering/emitter recursive frames are 64/368/160 bytes, bounded
by four if levels. The private IR capacity is derived as one invocation plus
sixteen statements at at most thirteen instructions each; value slots include
the unused zero ID. A worst-shape developer probe emits 2,916 of 4,096 words.
This is a footprint increase, not a performance or productivity advantage.

## Retained first failures and limits

The first strict build reported a maybe-uninitialized temporary in chained
lowering; explicit initialization fixed it without disabling the warning.
No semantic repair was requested by independent acceptance. Its first new
guard harness incorrectly expected every truncated prefix to reject: removing
only the final newline leaves valid WGSL. That test expectation was corrected
and its first failure retained; candidate hashes did not change.

This remains a bounded WGSL compiler subset and Linux x86-64 native boundary.
The existing browser corpus declares additional targets not supplied here.
No physical GPU, asynchronous runtime, barrier/uniformity, aliased-descriptor
ordering, application adoption, full target parity or full migration is
qualified. Production replacements remain zero. See the
[complete migration gates](../../../docs/c-migration/MIGRATION.md).
