# Instance 1 author implementation and acceptance requirements

Frozen contract inputs, not independently authored evaluator cases, an evaluator
verdict or a test implementation. No instance commands were run for this package.
The independent evaluator authors and seals decisive inputs/expected observations
before candidate implementation. Callback acceptance is established at
`3582e86472db945cc441eedb3796ec7f1b13d179`; [SOURCE_INPUTS.json](SOURCE_INPUTS.json)
binds the unchanged affected runtime/ABI/build inputs to that predecessor.

## Controls and attribution

Use four fresh, separately source-built native DSO selections from the same
frozen runtime baseline: default (Z), futures only (F), futures+callbacks (FC),
and futures+callbacks+instance (FCI). FCI is the only new owner. FC is accepted
at the bound predecessor, but the new instance/value-view integration must be
independently requalified across all four combinations. Also run standalone
GCC C instance tests linked to one accepted C futures archive, and a strict C++
header/link consumer. Active compiler work is separate and must remain untouched.

Exact observations include source/header/archive/DSO/consumer hashes, GCC/Zig
versions and command lines, target, environment, loader dependencies, first
failures and repaired reruns. No rebuilt artifact silently replaces a prior
receipt. Compile-only, real CPU behavior, injected boundary behavior and physical
behavior are different columns, never one aggregate success flag.

## Decisive native CPU matrix

1. Native and public actual instance creation, AddRef/Release, and repeated
   independent lifetimes; object descriptor ignored as before. Require the real
   C object allocator and queue-init path under FCI. Show exactly one final queue
   cleanup/label removal/allocation free using non-owning observations, not a
   dummy observer reference. Repeated lifecycle runs must return to the measured
   starting allocation balance. No post-free dereference to prove destruction.
2. Direct exports and actual wgpuGetProcAddress with explicit and sentinel name
   lengths for create/addref/release/wait/pump/requestAdapter/createSurface and
   feature/limit/free functions. Pin provider provenance; where current local
   resolver promises the exact exported function, compare pointer identity.
   Where the existing proc manifest intentionally maps to native bridges, compare
   the documented local target rather than demanding an invented equality.
   Genuine selected instance surface creation must never call the incumbent.
   A C owner symbol's existence alone is insufficient: invoke the real consumers.
3. Null/readable-invalid AddRef/Release no-op; null/readable-invalid nonzero wait
   Error3; count-zero Success1 before handle/infos inspection at zero, finite and
   UINT64_MAX timeout. Supply live/readable nonmatching-marker storage and actual
   live wrong-kind objects, never arbitrary or stale pointers. Wrong instance,
   unknown/zero/legacy futures follow existing error and flag behavior. Invalid
   ProcessEvents still drains the separately retained global work-done registry;
   establish a real registered no-instance obligation rather than only calling
   an empty fallback. Existing registry is not new C-payload evidence.
4. Exact unavailable-adapter path in a fresh process with DOE_BACKEND=metal before
   first request, through existing Linux metal_bridge_create_default_device:
   status Unavailable3, NULL adapter, exact `metal default device unavailable`.
   Modes0/3 immediate, mode1 wait-only, mode2 pump eligible. Callback releases its
   last caller instance lease. Require payload retirement/free before enclosing
   C return and instance finalization after full-call completion, no extra lease
   hiding either. Exercise no foreign callback, duplicate wait entries, nested
   requests/pump/wait and callback-issued follower deferred by the enclosing
   pump's captured issuance boundary. An explicit recursive ProcessEvents call
   captures its own fresh boundary and may deliver that follower; test that
   admitted behavior separately rather than requiring universal next-pump deferral.
5. Actual unconfigured surface: use real headless Linux artifact with its existing
   has_vulkan closure; create through wgpuInstanceCreateSurface with valid readable
   descriptor and native bridge's NULL device. No VkDevice, VkSurfaceKHR, runtime,
   swapchain, fake backend or presentation. Release the caller instance, assert
   no final free, then use a borrowed raw identity while the actual surface holds
   its lease to pump or submit the same real unavailable-adapter request. Deliver
   it while surface ownership remains, then release surface through public/native
   paths and establish final instance cleanup once. The request itself must not
   keep an independent forgotten lease past surface release. Label removal and
   exact allocation balance are observed without reading freed instance memory.
6. Surface failures: fail actual DoeSurface host allocation before retain and
   show unchanged instance count/no leak. Use existing native three-argument
   entrypoint with a readable invalid non-null device to take genuine bind
   rejection after retain; verify that retain is undone, surface freed and caller
   instance still works. Do not create a synthetic DoeDevice to pass configuration.
   Test actual native CreateSurface's historical null/invalid-instance behavior
   separately; do not redefine it to match the standard foreign fallback route.
7. Instance features exact zero/NULL, all defined and unknown HasFeature false;
   limits Success1, nextInChain NULL and timedWaitAnyMaxCount0; FreeMembers no-op.
   Preserve tests/instance_feature_lookup.c as a separately exercised original
   oracle, extend with exact limits/free/direct/proc observations. A finite wait
   result must not change advertisement. WGSL-language exports remain control.

## Queue-value and retained-owner obligations

8. Copy a QueueView from a short-lived stack scope, register an actual Completion
   against a real C instance, destroy/overwrite the local view value, then publish
   on a real worker. The payload owns its actual independent instance lease.
   Callback finishes/frees payload and drops that lease; readiness-call lease
   alone keeps C state alive until return. No stored pointer may name the old
   view or PendingCompletions wrapper. A copied view itself must not change count.
9. Late dispatch: allocate a real host completion payload, retain the real C
   instance, issue/register first, install dispatch later, publish, finish/free
   and release. Include ownerless preparation, finish-before-init and unregistered
   cleanup. Compile and trace actual compute/render pipeline registration and
   settlement callers with the new opaque instance and value owner. This adapted
   host completion fixture does NOT qualify a successful pipeline or fake device.
10. Exercise actual retained doe_device_loss.Event with a real C instance and
    null device: create its genuine pending obligation, release caller reference
    inside callback, finish and release through real instance hooks. Exercise
    failed event allocation before retain/ID issuance and exactly-once loss
    readiness. Use real ownership, no dummy device. This is the loss-event host
    owner's seam, not actual backend device creation/loss qualification. Preserve
    its old retained inline tests under default selection.
11. Keep all eleven accepted future-integration obligations and all five callback
    integration obligations. Under FCI, adapt ONLY direct Zig field access,
    selected creation and observational hooks into real opaque-instance fixtures;
    keep assertion meaning and error/ordering/lifetime coverage. Keep original
    Z/F/FC lanes available separately. List adapted vs unchanged tests/counts,
    including no-match filters or explicit unsupported steps rather than claiming
    execution. Original future_ids and callback_delivery implementation bytes stay
    unchanged. Any adapter amendment is independently requalified on F and FC.
12. Actual task-pool submission, real worker publication, failed-submit immediate
    fallback and shutdown joining retain all old callback obligations. Reentrant
    self-wait completes for delivering thread; other-thread wait must not observe
    retirement while callback is still active. Deferred allocation owns inline
    message/context bytes independently of source stack and typed resources.

## C count, allocation and exceptional cleanup

13. One C atomic count: real retain/release contention with a final owner held;
    exactly one finalizer after all workers join; no relaxed memory-order downgrade
    on release. Source-review and independent private reduced-limit builds cover
    max/zero guard without publishing a mutation API or using stale native handles.
    Count boundary fixtures are C-unit evidence, not fake native-object success.
14. External-texture guard C-unit hooks return controlled counts and verify the
    inherited count>1 CAS decrement/floor1 behavior. undo_retain performs an
    unconditional NONFINAL decrement despite a positive guard. Probe both actual
    external-texture rollback callsites by compile/source route review and retain
    physical success/failure suites as an unexecuted gate. Hook-only guard tests
    do not qualify real imported textures or the retained registry allocation
    failure behavior. No second registry, silently changed guard, or two counts.
15. Fail every native/C allocation point independently: object allocation and
    wait temporary allocation (separate identity/allocator), plus surface/event
    allocation at retained owners. Check exact matched allocator/state/vtable/
    extent/alignment; include stateless NULL state, controlled over-alignment and
    checked addition/alignment/multiplication boundaries. Returned errors leave
    caller-owned resources, flags and original valid instance untouched as specified.
16. Inject every accepted queue initialization boundary: mutex init, condattr init,
    clock selection, cond init, condattr destruction; show ordinary setup cleanup
    before one object free and a later successful fresh creation. allocated hook
    fires at the original before-init point and reports actual C allocation extent.
    No label-removal hook is invoked for unpublished ordinary setup rollback.
    allocated is observation-only for an unpublished/uninitialized identity.
    remove_label follows zero count and successful queue teardown, so its pointer
    is cleanup identity only: no retain, borrow, resurrection or instance API
    reentry. Deallocation likewise cannot reenter/resurrect the dying instance.
    These hook restrictions do not prohibit normal leased delivery callback reentry.
17. Subprocess diagnostic termination for cleanup-required init, BUSY/pending/active
    final deinit, mutex/condition destroy or unlock uncertainty, invalid refcount
    transitions and unexpected queue state. Preserve live synchronization until
    termination: prohibit label removal or allocator free after failed deinit.
    Inject platform boundaries/accepted queue status only; never replace the queue
    with a second behavior model. Capture operation/status and first failure.
18. Wait-copy overflow before reading records; OOM leaves sentinel flags unchanged;
    existing queue errors copy outputs exactly as old selected path. Duplicate IDs
    don't duplicate callbacks. Timeout preserves obligation; real worker wake after
    timeout completes and cleans up once. Preserve one monotonic deadline,
    issuance boundary and lowest eligible selection. Requesting INF doesn't let a
    callback's wall time be preempted or change timed-wait feature advertisement.

## ABI, build, regressions and instrumentation

19. GCC ABI probe binds sizeof/alignment/ALL field offsets and scalar widths for
    every crossing header record, allocator/hook signature, WGPUFuture/WaitInfo,
    feature/limit outputs and enums, plus real function-call/return tests. Existing
    completion/queue imported-layout probe remains for the amended future adapter;
    invoke it before selected native object publication even though C owns queue
    allocation now. No sizeof-only alias proof and no private Zig layout overlay.
20. Default native artifact has no C-owner imports/link dependency. F retains no
    C callback/instance dependency; FC retains no C instance dependency; FCI links
    one exact instance+callback+future archive set. Missing/relative archives,
    dependency omissions, unsupported target/browser-wasm and incompatible focused
    suite selection fail explicitly. No native object may mix identity domains.
    Check main/compute/full options and all configure-owner roots in build.zig,
    marking artifacts compiled vs actually executed. Headless execution alone is
    not compute/full/package/pinned-Dawn-proc-table qualification.
21. Run focused old default owner tests and adjacent lease/loss/pipeline cleanup,
    legacy work-done/reentry, label/trace, and allocator tests; retain original
    broader/physical suites without claiming unrun results. Source layout/import
    fence, formatting, charter/index and test-inventory generation checks follow
    repository owners. No tests are deleted because they don't fit opaque state.
22. ASan/UBSan and TSan where available: name EXACT compiled/instrumented objects.
    Standalone C instance+future tests can instrument both; a sanitized C archive
    inside a mostly uninstrumented Zig native DSO does not prove Zig memory/race
    safety. A failed sanitizer runtime is an unexecuted blocker, not a pass.
    Use actual final-release observations without observer references or leaks.

## Observation discipline and measurements

Test observation is non-owning and test-only. Preferred: allocator/hook counters
with distinct object/wait allocation identities and queue-call wrappers that
record entry/return ordering while executing the original queue. If an actual
DSO needs private counters, compile them only in explicitly labelled testing
archives/header, absent from the installed public owner header and normal build.
Never expose a production refcount mutation/getter or keep the decisive instance
alive merely to read its count. Verify final native behavior on the normal DSO too.

Record current task's discovery/decisive-test/build/test/blocked times, files and
owner documents inspected, exact clean/incremental costs, artifact sizes, source
and adapter deltas, retained Zig debt, ordinary allocation counts and regression
exposure. Report unmeasured metrics as unmeasured. This design alone claims no
performance, maintainability improvement or full runtime migration.

Before implementation, complete final exact-seal review and lead installation
of this already reviewed specification; the independent evaluator must separately
author and freeze its own cases. Callback acceptance is already confirmed at the
bound predecessor. Later instance acceptance is limited to its evidenced CPU
scope; all physical and adoption gates remain open.
