# Independent M9 candidate1 evaluation

Verdict: ACCEPTED against the frozen compiler contract. No open compiler defect
or unresolved required gate. Acceptance does not extend to physical GPU behavior.

## Scope and identities

Cloud-only compiler evaluation, accepted M8 baseline
4e03682849db8101cc8ed35a8243433c305852d5. Frozen contract SHA256
1b5072f1841a05e8e6feb294ecc5bc7b4a91edc3e7d14d83217fec8ccf34f24a;
independent preimplementation obligations SHA256
71e156278defd9eb29d35f78cdb8fd0605abf869d46c23382c82645f8306b2d1;
semantic/oracle preinspection seal SHA256
5f5bf082b26fd4cfa4edaae216bd4243e85656b9fa532c229b18bb8986b955e3.
Candidate1 manifest SHA256
55af8003b0611795585af60ae5f9881ae53d14b1936f6c02223e75f879b85937.
No implementation was inspected/executed before exact evaluator seals.

## Completed semantic and consumer gates

- Strict GCC source-build library and CLI pass; no Zig used.
- New231 positive/149 negative cases:380/380. All accepted modules validate
  explicitly for Vulkan1.1. Independent instruction execution:960 invocations,
  1,805 real user calls and maximum1,024 activation frames, plus typed signatures,
  parameter order, caller continuation, fresh locals and ordered resource effects.
- All10 unchanged witnesses pass independently packed finite expectations.
  Forward/pairwise attention keep their own arithmetic orders, not forced equality.
- Independent58 Decimal/Fraction enclosure controls and52 handwritten target
  controls pass. Numeric expectations do not compare host libm against itself.
  Abstract binary64 quality is observed through scaled residuals before f32.
- All12 new and17 retained emitted-module mutants independently validate then
  are detected by actual instruction execution, not by target rejection.
- Historical839 cases covered:837 pass in the immutable first receipt and the
  two predeclared infinite cases pass in a separately controlled adapter repair.
  All997 retained M8 cases,31 retained capacity and8 postdisclosure cases pass.
- Original full98 consumer:98 attempted,33 fresh emissions,33 Vulkan1.1 checks,
  33 instruction executions/12,258 invocations,65 explicit nonaccepting skips
  (64 parsing/1 semantic). Separate prebuilt observations:36 found/35 valid;
  the old draw_call_proxy_fragment.spv failure is not candidate acceptance.

## Native and target-format gates

The separate native package provides the detailed final receipt. Completed
checks include C/C++ source archive/shared/provided-prebuilt consumers,
new native/FP/errno/public-exit matrices, no-allocation/header/export checks,
ASan/UBSan, caller-memory guards and injected save/normalize/restore failures.
Measured fixed workspace119,408 bytes; ABI1 frame119,520; other maximum1,344,
within frozen122,880/123,136/8,192 ceilings. CLI174/174 checks pass. Both exact
derived M6 native runs pass (ordinary allocator-wrapped archive and sanitized
shared library), including all retained fuzz, mutation and concurrency checks.

Combined library-only stack bound: wrapper+6,152+max[368(D+2)+144(3C+4),
1216(D+2)+624(3C+4),256(2C+D+4)]. Fixed D18/C4 gives ABI1 159,976 bytes
and V2 40,568; max-job D64/C32 gives149,064. These are conservative
callgraph-derived bounds, not measured runtime peaks, and exclude libc/libm,
OS and sanitizer frames. Function/call-graph count adds no C recursion.
Detailed audit: native_capacity/candidate1-audit/stack-analysis.json, SHA256
6391a7cddd9664d150215f5c091e517865e53efe0eefe15af738f1539023226d.

Direct job format boundaries:65,532 formals pass,65,533 reject;65,531 actuals
pass,65,532 reject. Both positive modules target-validate with Vulkan1.1 and
explicit --max-function-args65532; giant-call validation completed in207.686s.
All41 compiler capacity statuses and18 query edges matched. The first default
validator120s observation remains inconclusive, not silently called a pass.

## Narrow repairs, not candidate changes

Original seals/sources and first failures are preserved. No production defect
has been established. The only evaluator repairs are explicitly named:

1. `retained_m9_corrected.py` restores the retained unfinished handling omitted
   by the new vector adapter, only for already-tagged fixtures. Four independent
   controls reject finite bound exhaustion and unexpected infinite termination.
   Every finite expectation is unchanged; two source hashes match first receipts.
2. Historical M7/M6 private workspace ceilings77,760/96,208 are replaced only in
   separately derived harnesses by the preapproved M9 ceiling122,880. Every other
   byte/assertion remains as documented by the native source manifests.
3. Existing configured validator resource allowance is also applied to512/2,048
   formal positives. Required large-call validation runs before an optional
   default-resource observation and has a bounded1,800s observation window.

Exactly five new named historical rejection lifts: M4 trailing_function and
unknown_call; M8 excluded_numeric_feature_4/5/6 (sqrt/exp/clamp). Prior documented
M8 lifts/errata are unchanged. M9 max chooses the right signed-zero operand on
an equality, matching its frozen deterministic policy and prior runtime target;
this is not mislabeled as a newly discovered conformance defect.

## Portable reproduction and evidence

Use `source/README_CURRENT.md` and the one current source-only gate
`source/run_acceptance_m9_current.py`; supply repo, compiler source root,
installed validator and fresh output. Sibling `native_capacity/source` is the
normal layout or use --native-source. --plan lists every command. No evidence
archive, prebuilt candidate, private path, download or GPU is required.
The original PRE_CANDIDATE_SEAL is full-evidence provenance, not a complete
portable package manifest; generated freeze receipts need not ship.
`source/CURRENT_SOURCE_MANIFEST.json` binds current orchestration and original
semantic sources;139 historical imports and22 original seal entries rechecked
unchanged. `CANDIDATE1_SOURCE_AUDIT.md` records the actual lowering/emission review.

First receipts are in candidate1-core, candidate1-historical,
candidate1-historical-unfinished, candidate1-m8, candidate1-mutants,
candidate1-m8-mutants, candidate1-retained-small and candidate1-corpus. Lossless
module compression and completed historical/M8 tar consolidation preserve exact
paths/sizes/SHA256 and verified extraction, with5,165,056 allocated binary bytes
reclaimed. All JSON receipts, seals, source and final DSO/archive are retained.

This establishes only the frozen compiler milestone on the tested Linux host.
It establishes no physical GPU numerical result, backend qualification, runtime
adoption, performance parity, browser ownership or completion of Doe's migration.
