# Futures integration 2: retained native consumers

Frozen before implementation on 2026-10-23; acceptance is not yet claimed.
Baseline C owner:
`b9b07dfa32313f074e6f34a1ccf516fc27075ada`. The accepted standalone C contract,
implementation and original Zig future owner remain intact. This increment is a
reversible experimental native-caller integration, not a second queue, default
provider change, full runtime migration or physical completion qualification.

## Actual owner and selected build closure

The actual retained `DoeInstance.pending_completions` and these five source
imports must consistently select one owner:

- `native/support/doe_native_object_types.zig`
- `native/support/doe_callback_delivery.zig`
- `native/support/doe_device_loss.zig`
- `native/queue/doe_queue_lifecycle.zig`
- `dropin/wgpu_dropin_ext_a_pipeline.zig`

Selection belongs to one new facade. Default remains the existing
`doe_future_ids.zig`, byte-identical to its accepted source hash. An explicit
experimental root/build flag selects a new Zig-to-C adapter. No artifact may mix
Zig legacy ID issuance with C queue IDs. The production main checkout remains
unchanged. Default builds must neither require nor link any C futures/bridge
symbol. The opt-in build must fail explicitly without its externally GCC-built
archive, rather than invoking Zig as the C compiler or falling back to Zig state.
The bridge is a retained Zig consumer of independent C, not a Zig-free full
runtime. Preserve that source/build/provider distinction in all evidence.

One actual instance allocation/final destruction seam exists in
`lifecycle/doe_instance_device_native.zig`: CreateInstance and InstanceRelease.
WaitAny and ProcessEvents already hold an outer instance reference. Existing
callback_delivery payload allocation, retained callbacks, request cleanup and
instance reference counting stay real, not substituted test models. Device-loss
and pipeline producer imports must type-check consistently; GPU/device/backend
behavior is not inferred from their use of the queue.

## Exact build and file route

- Add `native/support/doe_future_owner.zig` as the single facade and
  `native/support/doe_future_c_adapter.zig` as the narrow native adapter. Change
  only the five imports named above to the facade; preserve original owner bytes.
- Add private `runtime/c/futures/integration/doe_futures_zig_bridge.h` and `.c`,
  built with GCC together with the accepted C sources into a PIC static archive.
  The header exposes actual C storage types to the adapter, not public promises.
- Add optional `-Dexperimental-c-futures-archive=/absolute/path/to/archive.a` in
  `runtime/zig/build.zig`. Omission is the sole default and has no C includes,
  symbols or archive dependency. Presence selects one explicit build-options
  boolean in the main/compute/full drop-in modules, adds the private and public
  header include paths, links that exact externally built archive plus pthreads,
  and rejects unsupported non-native/non-Linux-x86-64 targets. No build command
  silently compiles the C owner with Zig. The qualified artifact target for this
  increment is the existing headless `dropin` shared library; other tier execution
  is not implied by consistent build wiring.
- Add a focused `test-c-futures-integration` build step and
  `runtime/zig/tests/c_futures_integration_test.zig`, using the same configured
  retained source modules/options and external archive. Only explicitly named
  integration tests execute; this is not a claim that the entire retained core
  suite runs selected. Default tests remain separately available with no flag.
- Change actual `lifecycle/doe_instance_device_native.zig` only at instance
  creation and terminal destruction for selected queue lifecycle. Selected
  facade initialization/deinitialization helpers are no-ops for retained default.
  Wait/pump and payload callbacks keep their actual existing bodies; bridge
  wrappers provide full-call leases and typed wait-record conversion.
- New integration source/build/native-consumer scripts remain under
  `runtime/c/futures/integration/`. A C consumer loads/calls actual native instance
  exports from the built shared artifact; Zig integration cases execute the real
  callback_delivery generic and actual instance exports, not replacement mocks.
  Test-only observer/injection hooks must not substitute release/refcount logic.

No unrelated production owner, compiler, backend, allocator or default build
policy may be changed. Any extra touched file must be traced and reviewed before
implementation, rather than silently widening this list. Output/install/cache
prefixes are new directories within the campaign; no baseline artifact is replaced.

## Private typed storage, initialization and ownership

A private native C bridge may expose the actual C-declared queue/completion types
from the same source build. The Zig adapter embeds those imported C types, never
casts a declared byte array using guessed size/alignment constants. Freeze and
test C/Zig sizeof/alignment/field offsets on Linux x86-64. This private FFI layout
is tied to the exact archive/header build and is not a stable public ABI. Retain
the standalone opaque public contract unchanged.

Initialize queue storage explicitly after actual instance allocation and before
CreateInstance returns success. No lazy hidden heap allocation, fabricated head
field, duplicate pending list or duplicated selection/delivery logic. Initialized
queue/completion objects cannot move/copy. Stack-only selected test instances
must explicitly initialize/deinitialize; their caller owns outer lifetime.
Default retained tests/receipts remain separate. Any adapted selected fixture is
labelled as such, not advertised as an unchanged test.

A Completion wrapper keeps its source-facing mode and current dispatch function.
Its C completion is explicitly initialized at registration or first ownerless
publication, with a stable C trampoline and wrapper userdata. This supports the
actual pipeline path that assigns dispatch after registration but before ready.
The trampoline snapshots the current function/userdata before calling it and
never reads the potentially freed wrapper afterward. It must not capture an
initial NULL dispatch as the eventual callback or mutate opaque C state.

Finish on a never-initialized wrapper remains a no-op. An initialized ownerless
completion is not exempt from cleanup. Every prepared completion is retired through the accepted C finish, then deinitialized before
its payload is freed; no C-owned synchronization resource remains in a payload.
No automatic deinit after callback return may dereference the freed node. Existing
exactly-once publication and external exclusion for manual retirement remain
caller obligations. Initialization/registration failures must not publish a
partial obligation or silently abandon a live completion.

## Full-call instance lifetime

The event-held lease alone is insufficient: actual callback_delivery dispatch
calls finish, frees its Node and releases its instance before it returns. C
mark_ready/process_events/wait_any still finish active-call accounting afterward.
Each dispatch-capable adapter call therefore snapshots its owner/lease hooks and
takes an independent temporary instance reference before entering C. It releases
only after C has returned, without rereading a possibly freed Completion.

This applies to asynchronous producers too, including publishMetalWorkDone,
pipeline completion and device loss; a lease only around callback_delivery.ready
would be insufficient. Existing public WaitAny/ProcessEvents leases remain.
Callbacks may release the last caller reference and free payload before return;
actual instance destruction occurs only after the final full-call lease ends.
Hooks for stack-only fixtures may be absent only with explicit caller-owned
quiescence. Do not extend payload lifetime merely to hide a dangling queue.

Final actual instance release deinitializes the selected queue before freeing
instance storage. Pending/active operations at final release are an invariant
failure, never permission to destroy synchronization. Existing external-texture
lease guards are preserved unchanged and are not qualified by this CPU slice.

## Failure mapping to retained APIs

Normal queue setup failure completes rollback and makes CreateInstance return
NULL without leaking the allocated instance or fabricating a usable queue.
No new ordinary allocation failure may be hidden as a panic. Unexpected
CLEANUP_REQUIRED from setup/destruction cannot be converted into successful
cleanup or freeing reserved storage. The mapping for these exceptional
pthread cleanup/invariant failures is fail-closed diagnostic termination, because
the retained public final-release API has no status channel. This mapping must be reviewed
before freeze and tested in isolated subprocesses before acceptance; no retry/recovery guarantee
or silently leaking live queue is claimed. The C standalone API still retains
its separately tested cleanup-only retry contract for callers able to use it.

As in the standalone API, SYSTEM_ERROR after readiness/retirement does not imply
transactional rollback. Do not repeat publication or free unknown live state as
error recovery. Existing infallible identity/registration/publication functions map identity
exhaustion and impossible/invariant statuses to explicit fatal diagnostics, not
successful fake IDs or fallback. Ordinary wait errors remain fallible and map to
existing WGPU wait error, timeout and success results.

WGPUFutureWaitInfo and the C wait record are not presumed C alias-compatible even
if sizes match. The adapter may use a checked, explicitly owned temporary C-record
copy for WaitAny, copying all original completion flags in and resulting flags
back. This is caller-adapter allocation, not library allocation. Its allocation
failure returns the existing wait error without touching original flags; count0
retains the existing success/no-instance special case. Checked multiplication,
cleanup after every path and allocation fault injection are required. Do not
split waits into chunks, change lowest requested selection, or cast unrelated
record types to avoid owning this adaptation.

## Build and test acceptance

1. Source/hash trace binds all changed imports, original owner, accepted C owner,
   private bridge/header, exact external GCC archive and actual Zig consumer.
   The opt-in native artifact uses one C identity domain; default artifact has no
   C owner linkage/dependency. Build cache/output stays in the isolated experiment.
2. Actual native CreateInstance/AddRef/Release/WaitAny/ProcessEvents and actual
   callback_delivery.publish/ready execute selected C state. A new C ABI consumer
   exercises native exported functions without GPU initialization. Existing
   retained baseline tests run separately; adapted selected tests are identified.
3. Payload allocation failure, queue setup failure/rollback, temporary wait-copy
   allocation failure and exceptional cleanup diagnostics have exact outcomes.
   No partial registration, leaked instance, premature free or false success.
4. Callback frees payload and releases last caller instance reference during
   immediate/spontaneous publication, deferred WaitAny/ProcessEvents and genuine
   worker publication. Reentry/self-wait/another-thread waiter, duplicate wait,
   readiness/issuance ordering, callback-before-finish and final destruction after
   full C return are checked with actual reference/object cleanup, not observer
   refs keeping the decisive object alive. Unfinished obligations remain pending.
5. Pipeline delayed-dispatch assignment uses stable trampoline; ownerless paths
   and requests freed before registration never access uninitialized C storage.
   No device execution is needed or claimed. Any linked producer path lacking a
   test is explicitly unqualified, not silently inherited.
6. Strict C/C++ bridge consumption, C/Zig layout proof, independent cases and
   applicable ASan/UBSan/TSan observations with exact instrumentation boundaries.
   A sanitizer on the C archive alone does not instrument all retained Zig code.
7. Source-only portable reproduction and exact first failures, costs and scope.
   No production-default switch or backend/device/GPU qualification.

Independent lifetime/build-seam review accepted this boundary before implementation.
All artifact combinations that actually select C must receive matching headers and
archive or reject the unsupported combination explicitly. Shared build-options or
source modules must not accidentally select an unlinked or mixed provider.
Implementation and independent acceptance remain pending. Compiler M7 stays disjoint.
