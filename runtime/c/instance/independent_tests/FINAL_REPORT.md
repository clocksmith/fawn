# Independent Instance 1 evaluation

## Verdict

PASS for the explicitly selected, source-bound C instance owner on Linux x86-64,
ReleaseFast, in the frozen CPU/host-ownership scope. No candidate implementation
change was needed after handoff. Default-provider selection is unchanged.
Debug, physical GPU/device/pipeline/imported-texture/presentation, package,
pinned-Dawn-proc-table, browser adoption and complete runtime migration remain
unqualified. The C owner is not a substitute for its retained Zig consumers.

## Identity and independence

- Accepted callback predecessor: `3582e86472db945cc441eedb3796ec7f1b13d179`.
- Original eight-input contract seal:
  `f224ddda8da7c7c0cea8b7dc2bbe4f5ad3771dce01fd6a65130d905037281919`.
- Independently authored 32 concrete case groups were sealed before implementation
  (`CASES_SEAL.sha256`, digest
  `eceea652086a4bbd9ce3d5f4d3233824f34179b848e414fd5d7ff4d6e6ae57fb`).
  All 123 accepted baseline Git inputs were verified. Baseline clarifications,
  original fixture versions and their seals are retained; later supplements do
  not retroactively become blind tests.
- Fixed native handoff seal:
  `51347ba22f88c12724af95a6d87d821615b86782855ca8b4fa1e892cbe584669`.
  Original 1,693 dependencies were augmented, not silently replaced, by 26
  required tracked Lean provenance files, byte-identical to both pinned
  `1aa0114489c0962108b71cbbde422b45c64053d3` and the accepted predecessor.
  Complete 1,719-input manifest digest:
  `f1d5323d7b73c96ad4f2d4ff43f0b55c7c01445f2383ad1d883bc2c580e2db7f`.
  This is source closure, not a Lean execution claim.
- All 1,719 input bytes reverify in all three private source copies. The 36-path
  implementation snapshot also rechecks against the current tree: 35 match;
  MIGRATION.md differs only by the separately disclosed M10 compiler paragraph.
  Acceptance stays on the fixed snapshot, not a moving whole-checkout HEAD.
  Compiler M10/M11 and later callback Makefile maintenance are not native inputs.

## Executed evidence

| Boundary | Independent result |
| --- | --- |
| GCC-only public C core and strict C++17 caller | PASS: normal 37 commands; ASan+UBSan 18; TSan 18; separately labelled author-prebuilt 30. Real accepted future queue, matched object/wait allocator identities and layout, 257 lifetimes, 8×10,003 retain/release pairs, full-call finalization, error flags, reentry, ordering, overflow/OOM and fatal platform cleanup. |
| Private count/status boundaries | PASS: 14 count build/control/fatal commands and 6 status/order/active-queue commands. No public count mutation API. Reduced maximum 3 uses the test-only limit constant 4; normal UINT32_MAX guard is separate. |
| Prior owners | Accepted future independent 53-command baseline suite plus callback developer checks pass; original future_ids/callback_delivery implementation bytes remain unchanged. |
| Four fresh source-built normal DSOs | Z/F/FC/FCI each pass strict C and C++ direct exports, both actual proc-name length routes, native exports, exact Linux Metal-unavailable adapter path in every callback mode, callback caller release, reentry/duplicates, empty instance features/limits and real unconfigured surface retention. Original instance_feature_lookup.c is separately executed. |
| Normal label/trace controls | All four providers pass 64 labelled lifetimes each, including sole surface-held leases; final label lookup uses an integer key, never a freed-object dereference. Both C/C++ consumer traces preserve original logical DoeInstance name/schema/sequence and actual allocation extent. |
| Author-prebuilt controls | All four final canonical author DSOs independently consumed with the same public/label/trace obligations, separately attributed. They never substitute for source-built controls. |
| Independent retained host fixtures | 12/12 pass: overwritten short-lived QueueView value, late dispatch, real publisher/task pool/fallback/shutdown, final-caller free, self/other-thread waits, native wait allocation/flags, actual null-device loss Event, actual unconfigured surface lease/allocation/bind failure and matched destruction, real ownerless global work-done draining. |
| Retained integration/default tests | F 11/11; FC 11 future +5 callback; FCI 23/23, consisting of 11 adapted future +5 adapted callback +7 additional owner obligations. Default 24 explicit original future/callback/loss/lease/pipeline cleanup/work-done/allocator/trace tests pass. No empty filter is counted. |
| ABI | GCC/C++ full crossing layouts/enums/signatures/by-value calls and frozen GCC/Zig probes pass. Additional retained manual-extern field size/alignment/offset comparisons and actual C calls pass 1/1; accepted queue-layout verification executes. |
| Native sanitizers | 12/12 under ASan+UBSan and 12/12 under TSan. Instrumented C units are instance.c, wait.c, capabilities.c, callbacks.c, storage.c, futures.c and doe_futures_zig_bridge.c. Retained Zig, C platform stubs and system libraries are NOT instrumented. ASan uses its actual GCC runtime first; leak detection is disabled, not qualified. |
| WGSL control exports | Strict C/C++ direct enumeration/membership/free observations match Z in all source-built and separately prebuilt providers: feature list [2]. Extra WGSL proc names are unavailable for both name-length forms in every control, explicitly reported rather than counted as calls. |
| Build closure | 13 incompatible selections reject with required diagnostics. Canonical main DSO and named compute/full compile/install pass; latter two are compile-only, not executed. Three options/module roots and all 36 configured artifact roots are source-audited, not all built/executed. Incremental main rebuilds preserve hashes. |
| Structural | Source layout, import fence, changed Zig formatting, generated test inventory and 29-charter/index checks pass under their recorded tool/input scope. |

## Ownership finding

One opaque C allocation contains the sole atomic instance count, copied config
and the queried, aligned accepted C queue storage. No extra queue/count/Zig shell
or scheduler was introduced. Native creation uses the actual C allocator/setup
path; native retain/release/wait/pump delegate to that owner. QueueView copies are
non-owning values with an explicit lease, not pointers to temporary wrappers.
Readiness snapshots its lease before callbacks can free payloads; wait/pump keep
full-call protection and free temporary wait storage before the final release.
Finalization requires successful queue teardown before label removal/free.

Actual callback, null-device loss Event and unconfigured surface owners preserve
independent leases. Both real external-texture rollback callsites compile through
unconditional NONFINAL undo; positive external-count guard semantics are tested
with controlled C hooks only. Actual imported texture success/failure is not
claimed. Compute/render registration and settlement preserve late dispatch and
same-instance pending ownership; no fake successful device/pipeline was used.

## Preserved failures and repairs

1. First clean native package lacked the 26 Lean provenance inputs. The failed
   build and original seal remain; the verified additive closure is explicit.
2. Z's first two canonical attempts reported compiler-child termination with
   driver exit 1, without a known child signal/root cause. A separate exact
   build-lib diagnostic (only removing --listen and choosing separate output)
   passed, then the real canonical build and consumers passed. Diagnostic output
   was not substituted for canonical acceptance.
3. First FCI attempt hit its 900-second Python subprocess bound, outer exec 1.
   Native child return/signal and descendant shutdown were not observed. Closed
   session/different PID namespace is not proof of quiescence. One distinct
   source/cache/output retry passed in 417.057 seconds; its supervisor observed
   an empty process group afterward. No proven-uncontended or OOM/root-cause claim.
4. Two specifically approved generated-cache reclaims preserved all sources,
   snapshots, tools, receipts and installed outputs. The first removed six
   completed Z/F/FC caches (391,577,600-byte observed free increase). The second
   removed four regenerated completed F/FC caches after preserving and hashing
   29 generated files, including all three executed test binaries
   (202,625,024-byte observed increase). Active/uncertain FCI caches were untouched.
   Process/CPU/wait/RSS and accessible resource observations are retained; global
   VM counters are not attributed to a particular process or a causal diagnosis.
5. Default supplemental root first linked incompletely (five buffer/encoder
   exports missing). Importing the two actual retained owners, not stubs, fixed
   only evaluator closure. The same 24 tests pass; failed root/receipt remain.
6. Native sanitizer runner first chose the resolved runtime symlink's wrong
   -l directory, then ASan rejected runtime load order before tests. Correct GCC
   library directory and explicit runtime-first execution of the exact built
   binary fix only the harness; no check is disabled to manufacture a pass.
7. WGSL supplement initially required proc availability even on Z. The repaired
   control records actual missing routes and still requires equal availability
   and direct values across providers. Mandatory instance proc coverage remains.
8. Earlier readable-device storage/proc-target baseline corrections, native Zig
   fixture type fixes and surface allocator-route disclosure are individually
   documented with original versions. The author's logical trace-name fix was
   disclosed before final source handoff and then independently verified.

Debug remains unqualified. The author's matched Z/FCI Debug getProcAddress crash
controls used one contemporaneous pinned candidate tree, NOT a separately built
accepted-predecessor checkout. They establish no selected-owner-specific
regression, but do not prove all historical baselines fail or diagnose the cause.

## Measurements and retained debt

Successful main canonical/incremental seconds: Z 58.766/0.084;
F 66.311/0.082; FC 66.368/0.099; FCI 417.057/0.105. Failed attempts,
cache changes and exact argv/environment remain separate. These are observed
mixed-state task costs, not controlled performance comparisons.

Actual instance allocation bytes: Z40, F176, FC176, FCI264. Exact DSO/archive
hashes/sizes and production C/adapter line extents are in MEASUREMENTS.json and
GCC_CORE_RESULTS.json. No allocation-latency, maintainability or performance gain
is asserted; exact discovery/authoring durations were not measured.

Zig still owns adapters/devices, typed callback resources/task pool, loss Event,
surfaces/runtime resources, labels/traces, external-texture registry and physical
backends. All broader physical/adoption gates remain open. This is one accepted
experimental CPU ownership boundary, not complete migration.
