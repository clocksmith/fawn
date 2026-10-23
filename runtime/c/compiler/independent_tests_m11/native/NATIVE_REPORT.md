# M11 candidate2: independent native verdict

PASS within the GCC14.2/Linux x86-64 native boundary. No candidate defect or remaining unqualified native gate was observed. This is not the parent's full semantic/interleaved-target acceptance.

## Evidence and correction history

- `NATIVE_VERDICT.json` is the structured verdict and evidence index.
- Immutable source manifest: `b124146a5044e288dcd7fbd153a7f75f3ecfc2e4451666b2f4e1b7771073b9af`. All30 source entries remained unchanged through final recheck. Fresh strict archive/shared/CLI build, four unchanged public headers, exactly seven exports, no allocator imports: `candidate2-build/`.
- First113-command native run is retained as FAIL:109 commands passed; four failures were the same evaluator boundary supplement across archive/shared/author-prebuilt/sanitizer. The supplement wrongly equated public alignment16 with private alignment8 and supplied unselected word fields in two REFLECTION-only descriptors. Only those four assertions/selections were corrected. Original source, hashes and failures remain. Affected eight build/run commands then passed: `candidate2-boundary-revision1/`. Accepted M10 query evidence is in `ACCEPTED_M10_ALIGNMENT_EVIDENCE.json`. Public16 is sufficient/divisible by private8, not equal to it.
- Unchanged accepted M10 and narrowly mapped older native C/C++ inputs passed across archive/shared/prebuilt routes; host state, errno/fenv, output atomicity, exact/short/stale spans, reuse, concurrency and allocator self-controls passed. ASan/UBSan affected gates passed. Historical mappings were enumerated before adapters: `HISTORICAL_MAPPING_BEFORE_ADAPTERS.json`.

## Frozen ledger and budget

- All23 actual records and every exact slice for fixed146256, typed CLI2536128, old-job CLI2172992 match the frozen formula. Secondary scratch poisoning and protected persistent-data hashes pass before/after uniformity.
- Complete14-unit GCC `.su`/call graph covers145 frames; ABI1 wrapper146400, conservative sum of every nonwrapper frame15200; fixed combined204608<=212992, max-job193376<=196608. Every recursive family and caller-owned wrapper meets its frozen ceiling. Raw evidence: `candidate2-GCC_STACK.tar.gz`; full graph/arithmetic: `candidate2-LEDGER_STACK.json`. Stack bounds exclude external libc/libm/OS/sanitizer/caller frames and are not observed peaks.
- Exact N/N-1 private link-time controls passed for all17 witness/helper/control sources and six output modes, also under ASan/UBSan. Six distinct mandatory counts: zero0, nonatomic1=35585, nonatomic100=80081, pairwise59473, matmul77198, vec4=59434. Larger formal/nesting counts and target IDs/words are in `candidate2-private-work.log`.
- All four public adapters use the same counted-work failure path: exact N succeeds; N-1 returns UNSUPPORTED, exact work-limit diagnostic, original source offset and no output. ABI1/2 N2325; ABI3/4 N23957: `candidate2-private-adapters.log`.
- The unchanged public67108864 cap is reached by the1000-formal/24-loop fixture through ABI3 and every ABI4 mode on archive/shared/prebuilt/sanitizer. Exact diagnostic/status/atomicity and same-pool no-barrier recovery pass: `candidate2-public-hardcap/`. This fixture reaches an earlier unchanged fixed source/token INPUT_ERROR on ABI1/2; that is explicitly not reported as public hard-cap exhaustion.
- Important boundary: `uniformity.c:410-415` performs an **uncharged** callee-marker pre-scan bounded by `expression_count <= physical_expressions`. Budget creation is later, at line416. This is marker classification for the no-resolved-barrier fast path, not an AST dependency visit or call-graph traversal. Counts exclude those reads. Subsequent sensitivity operations are charged before execution; no-barrier work is0. Capacity-only loose bounds are computed with saturating u64 solely for receipts, not admission: `candidate2-LOOSE_WORK_BOUND.json`.
- Exact target header highest-ID+1, selected opcode/root counts and retained U32 target-format exhaustion controls pass: `candidate2-TARGET_COUNTS.json`, `candidate2-ID_BOUNDARY.log`.

## Qualifications

Full-capacity source/workspace/output/result nonoverlap is the public caller precondition, not a promised arbitrary-overlap rejection check. No GPU, alternate host ABI, TSan race proof, performance parity, runtime integration or whole-repository acceptance is claimed. Parent owns SPIR-V validation, interleaved execution and the full corpus. Author prebuilt provenance remains distinct from the fresh source build even though shared-library hashes match. No production source or retained historical input was changed.

All compact source, original failures, corrections and receipts are bound by `NATIVE_ARTIFACTS.sha256`. Heavy binaries and extracted source remain in the separately named `/tmp/doe-m11-independent-native-build2` and `/tmp/doe-m11-independent-native-candidate2` directories.
