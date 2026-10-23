# M11 candidate 2: independent evaluation

## Verdict

**PASS within the frozen standalone compiler, independent CPU target model, and GCC14.2/Linux x86-64 native scope. No required gate remains unqualified. No candidate defect was observed.** This verdict includes the original long shared-memory execution, not an elapsed-time estimate or validation-only substitute.

`VERDICT.json` is the structured result. `candidate2-observations/SOURCE_BUILT_CASES_FINAL.json` and `SAME98_FINAL.json` hold the complete semantic and per-source corpus accounting. `native/NATIVE_REPORT.md` and `native/NATIVE_VERDICT.json` give the native/ledger result and its qualifications.

## Identity and independence

- Accepted M10 base: `2220c64cff0bd87b1eed6b246515941ce7d18d24`.
- Frozen 17-input contract manifest: `9f5013276a311c610cade91062426b9ab6d7f993293c7fcd22ff183902c81f75`.
- Candidate source manifest: `b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af`; source archive: `c3ddb0594195aff3d157f7ee5cc488c4df7289f6c91f6b2dbe49c0302ba6172a`.
- Original independent case seal, before author implementation release: `9a03dd3f38641834f59a70e6f09a1524a5271daeb1886651ff7575b7a0a2d664`. The final pre-observation supplement seal is `4d5ac2a45fbaba470387b50626149fd69ee2568eece1a24b803d90b6b25610a6`.
- Candidate observation began only after the explicit immutable handoff. One fresh strict GCC build used the extracted, verified source. Fresh shared library: `5d301e813223c9602f7c7ad57634b0ad53a68ba8b752066fbfdce1e9f29b2553`; archive: `8f11e695ee777494e2660f51be02e2bd9b49ecacf3edd068ca77a868128e4273`; fresh CLI: `c927324f9d7ae2fd9ac14d61cb9334a9870c3ff8396e5934470bfb3653310beb`.
- Handed author-prebuilt observations are separately identified. Matching shared-library hashes do not turn prebuilt observations into fresh-build evidence. Four public headers and seven exports are unchanged.
- Final checks matched all30 source entries both in the held checkout and independent extraction, and all17 contract entries. No production or retained historical source was edited by this evaluator.

## Source semantics and emitted target

All120 effective frozen/supplemented cases passed the fresh source build. Negative cases checked the exact status, diagnostic ownership, original source offset bounds and no publication in all six output modes. Positive cases checked exact measurement/publication counts, descriptor reflection closure, actual Workgroup roots and pointers, no fake bindings, whole typed `OpConstantNull` stores, lane-zero selection, and exact `ControlBarrier(2,2,0x108)` placement and operands. Every emitted case passed the approved default Vulkan1.1 validator.

Uniformity expectations were independently derived from the dated WGSL2026-10-21 graph rules. There are93 explicit decisive dependency-graph slices, evaluated by a separate reachability oracle, not a copy of the author's Boolean solver or retained Zig acceptance. Coverage includes syntactically present constant-skipped RHS/calls, short circuits, formal and return dependencies, helper chains, whole versus partial assignments, readonly/mutable/workgroup roots, returns, continuing/continue/break/backedges, and valid divergent barrier-free reconvergence. Four graph mutation checks fail in the intended direction.

## Interleaved execution and self-controls

The executor interprets the actual emitted SPIR-V with suspended per-invocation environments, Function memory and call stacks. Workgroup memory is shared only within its group. Dynamic barrier rendezvous includes target instruction, caller chain and active loop iterations. Race checking recognizes same-vector component conflicts and orders only Workgroup memory, never storage-buffer accesses merely because a workgroup barrier occurred.

The frozen round-robin, reverse, seeded-shuffle and bounded-burst schedules were retained. All execution scenarios used repeated dispatch with reused, re-poisoned shared storage. Small fixtures ran at one-instruction scheduling granularity and were cross-checked at event granularity. Large real witnesses yielded at every observable memory access/barrier and after at most64 pure instructions, including nested calls. Exact output bytes and target instruction counts matched the small-fixture cross-check.

The main matrix contains24 execution scenarios:176 successful dispatches and16 expected race detections. Successful dispatches accounted for3,513,581,992 actual target instructions,107,337,136 scheduling events and106,430,424 observed memory accesses. The zero-init and pairwise real witnesses used one group because their outputs are not group-indexed; other appropriate scenarios used isolated multiple groups. Partial/unequal lengths, typed nested/vector-safe initialization, helper shared identity, whole-value snapshots, early returns, runtime bounds and skipped runtime side effects were checked.

All17 original self-control obligations closed. Removed initialization/barriers, skipped arrivals, privatized/aliased shared memory, wrong scopes/semantics, and helper/loop phase mismatches fail the intended check. Hand-assembled validator-valid helper and loop mismatch controls prove that static-barrier counting is insufficient. Actual candidate targets also detect same-vector and unfenced storage races. Validator-invalid execution-scope mutation is distinguished from validator-valid memory-scope/semantics mutations and semantic failures; none is hardware evidence.

### Unchanged100-loop real witness

`workgroup_non_atomic_1024x100.wgsl` ran unchanged under all four schedules, two poisoned dispatches each, with512 invocations across two isolated groups. Target SHA256: `44af617c0e73fdc3dcaaa2569e89406e95c7ca480f2d0813af183de69cb073d5`.

Each of its eight dispatches executed435,226,078 target instructions,13,262,328 scheduling events,13,159,926 memory accesses and201 dynamic barriers per group. All expected bytes matched, with no race or rendezvous failure. Total case elapsed time was5084.373908 seconds, about84.74 minutes. The original session exited0 and completed the remaining cases normally. No restart, schedule/iteration reduction, single-invocation replay, or validator-only substitution occurred.

There was no evaluator-installed wall-clock timeout. The fixed fail-closed bounds were100,000,000 events per dispatch and500,000,000 instructions per invocation. The original runner exposed no live intra-case progress counters; separate tool executions could not inspect its PID namespace, and one read-only sampling attempt failed during sandbox setup. Those limitations and the absence of a wall-clock timeout were recorded without treating elapsed time or active-session liveness as progress proof. Terminal counters, not speculation, now close the gate.

## Native, capacity and historical gates

The native verdict passes source/archive/shared/prebuilt C/C++ consumption, current-query/exact/one-short/stale spans, reuse/concurrency, errno/fenv, all-output-mode atomicity, allocation self-controls and ASan/UBSan. All23 private records and every phase slice match the frozen arithmetic: fixed146256, typed CLI2536128 and old-job CLI2172992. Public query alignment16 safely satisfies private alignment8 where applicable; equality was never required.

The complete145-frame,14-translation-unit GCC audit includes all recursive families. Conservative combined bounds are204608 fixed and193376 job, below212992 and196608. External/system/caller/sanitizer frames are excluded, and these are bounds rather than observed peaks.

Private exact-N/N-minus-one controls cover17 witness/helper/control sources and all six output modes, also under sanitizers. Shared-limit failure behavior is checked through all four public adapters. Public67108864-work exhaustion is verified on ABI3/all ABI4 modes, with the same-capacity no-barrier twin succeeding; ABI1/2 correctly encounter their earlier fixed pools on that large source. The bounded initial callee-marker pre-scan is explicitly uncharged and outside sensitivity-operation counts; no-barrier work remains zero. Target count/ID and retained overflow controls pass.

All576 retained M10 semantic cases are accounted for:574 passed with accepted-M10 expectations unchanged, one passed the separately presealed workgroup2-to0 admission lift, and the one exact private-global case received the contract-directed2-to3 mapping described below. The first M11 regression report therefore preserves575 passes and that one private-status failure. Original M10/M9/M8/earlier inputs, accepted adapters and first failures remain preserved, with no broad skip or accepted-status set.

## Fresh same98 accounting

Fresh independent CLI results: **52 emitted,52 validated,43 independently executed,46 rejected**. The43 comprise all37 retained M10 witnesses and the six mandatory new real sources. All37 retained target byte streams are unchanged from the accepted source-built M10 portable replay and were independently re-executed. Exact target hashes bind each new mandatory CLI emission to its completed interleaved proof; the pending long-case receipt is preserved separately from final reconciliation.

Nine additional newly emitted sources are validation-only, not instruction-executed acceptance. Their exact paths appear in `VERDICT.json` and `SAME98_FINAL.json`. Rejections are not passes, and unrelated prebuilt modules are not added to fresh-source coverage.

## Preserved corrections

Before candidate observation, separate mappings corrected three mistaken numeric UNSUPPORTED labels, a literal out-of-bounds positive into its required semantic negative plus a runtime-index variant, eight reserved `shared` identifiers, and one reserved helper identifier. Original fixtures/seals remain intact; identifier changes did not alter types, control, inputs or expected bytes. Accepted-M10 controls proved only explicitly transformed syntax/type behavior, never original Workgroup-module admission. Four additional execution fixtures and the bounded event-scheduling supplement were sealed before observation.

After candidate observation, the exact otherwise-valid private-global source hash `20ee474ecf0eca32a4333dfe47989e52a825b72a8a895742e4d26ec9201e6160` changed only its expected2/PARSE_ERROR to3/UNSUPPORTED, as required by frozen M11 lines71–75. Its original failure, accepted-M10 control and six-mode candidate observations are preserved. No malformed/unknown address-space class was remapped.

The native first113-command report remains a FAIL with109 successful commands and four failed routes through the same evaluator supplement. Only two alignment assertions and two REFLECTION-only descriptor selections were repaired; affected routes then passed. Original source, failures, correction receipts and the stack-parser naming repair remain available. These are evaluator/intentional-contract corrections, not compiler fixes.

## Scope limits

This establishes standalone compiler behavior under the stated independent CPU model and target validator, not physical GPU behavior. It does not establish Vulkan/Metal/D3D12 runtime adoption, native workgroup metadata integration, hardware correctness, alternate-host ABI, TSan proof, performance parity or whole-Doe migration. Full-capacity source/workspace/output/result nonoverlap remains the caller precondition.
