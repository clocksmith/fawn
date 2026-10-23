# Callback payloads: retained-consumer seam

Frozen after independent contract review, not implemented or accepted. Baseline is native queue integration
`6b9fea1d581f4d78a7a86a76beddd5a43ff1a965`. This contract complements the
[plain-C ownership contract](../CONTRACT.md) and preserves the accepted
[futures integration](../../futures/integration/CONTRACT.md). Independent review,
lead freeze and evaluator inventory precede implementation.

## Exact production and test edits proposed

Paths in this section are repository-relative. No edit has happened at these
seams yet. Additional production changes require a source trace and review before
implementation, not post-hoc scope expansion.

1. Add `runtime/zig/src/native/support/doe_callback_owner.zig`, a selection facade.
   Default `ready`, `publish` and `instanceForDevice` use the original
   `doe_callback_delivery.zig`; its implementation AND two inline tests stay
   byte-identical. The facade exposes `Publication` and `markReady` as the sole
   publication-token boundary. Default Publication is the existing selected
   future owner's Completion. C-selected Publication is the opaque C event.
   No extra allocated or borrowed Zig Completion wrapper surrounds a C event.
2. Add `runtime/zig/src/native/support/doe_callback_c_adapter.zig`, containing only
   the typed Context assignment/deliver thunks, copied Allocator hooks, copied
   native queue lease binding, publisher thunk and exact status/error mapping.
   It creates a mutable local Context for C's ownerless path. C alone decides
   mode/admission/ownerless delivery and owns the allocation/registration/cleanup.
   `instanceForDevice` can remain an alias to the unchanged retained helper;
   do not duplicate device/adapter topology or move typed resources into C.
3. Change only the callback-delivery import to the new facade in these five files:
   - `runtime/zig/src/native/lifecycle/doe_instance_device_native.zig`
   - `runtime/zig/src/native/resource/doe_buffer_ops_native.zig`
   - `runtime/zig/src/native/queue/doe_queue_lifecycle.zig`
   - `runtime/zig/src/dropin/wgpu_dropin_ext_a_exports.zig`
   - `runtime/zig/src/dropin/wgpu_dropin_ext_b.zig`
   Keep their typed Reply/deliver/catch bodies and public WebGPU signatures intact.
4. In `doe_queue_lifecycle.zig` only, additionally change publishMetalWorkDone and
   its task-pool job's token cast from `*future_ids.Completion` to
   `*callback_delivery.Publication`, and call facade markReady. The task pool,
   allocator, dispatch predicate, exactly-once submit/fallback and queue resource
   release stay unchanged. Do not cast C event storage to a Zig Completion.
5. Add `pub fn borrowedQueue(self: *PendingCompletions) *c.doe_future_queue` in
   `runtime/zig/src/native/support/doe_future_c_adapter.zig`. It returns the actual
   initialized accepted-C queue via the existing guarded queue() function. The
   callback adapter requires the actual instance pending.lease to be non-NULL and
   copies its context/retain/release identities into the public C
   owner record. No inferred offsets, second queue, lease removal or ordinary
   native object/refcount rewrite. Crossing this real queue to its opaque C handle
   is not a cast between unrelated completion layouts. All existing future
   markReady/wait/pump full-call leases remain intact for every other producer.
6. Update `runtime/zig/build.zig` with the explicitly default-off callback archive
   option and selected test step below. Existing main/compute/full source modules
   and their native artifact closures must receive matching options/includes/link
   dependencies together. Unsupported combinations reject instead of selecting
   unlinked modules or hidden fallback.
7. In `runtime/zig/tests/c_futures_integration_test.zig`, import the new delivery
   facade and use its Publication/markReady only for delivery-publisher variables
   and worker function arguments. Preserve all eleven current tests/assertions;
   direct future-adapter Completion tests for delayed dispatch, ordering and
   ownerless completion cleanup stay on that actual future adapter. Run this
   same adapted suite with futures-only and callbacks+futures selection. Do not
   call these adaptations byte-identical legacy fixtures.
8. Add `runtime/zig/tests/c_callbacks_integration_test.zig`, register it in
   `config/zig-test-inventory.json` and generate its suite root
   `runtime/zig/test_suite_c_callbacks.zig` through the existing generator. Tests
   execute actual instance exports, selected payload owner and task-pool transport.
   Any fixture observation owns no extra decisive reference and never substitutes
   refcount/destruction logic. Prefer the existing deinit counter over a new
   lifetime-observer framework.
9. New C developer tests, archive/reproduction scripts and actual C ABI consumer
   belong under `runtime/c/callbacks/`. Build outputs/evidence stay in separately
   named campaign directories, not retained baseline paths. Keep the immutable
   queue/core/header/bridge implementation and existing independent evaluator
   sources unchanged. Document new acceptance only after actual evaluation.
10. For freeze/admission, update only applicable boundary/navigation records:
    `runtime/c/callbacks/CATSCAN.md`, generated `docs/component-index.md`,
    `docs/c-migration/MIGRATION.md` and new owner documentation. In
    `runtime/zig/source-layout.json`, add the new callback adapter to the explicit
    `architecture.specialRoles.ffi-boundary.globs` list beside the existing future
    adapter, add source-bound small-module decisions for the two new production
    paths, and refresh the changed future adapter's decision/hash after review.
    Native owner/layer prefix rules remain unchanged. Regenerate
    `runtime/zig/src/README.md` with the existing generate_source_layout_docs.py
    owner if its output changes. Do not edit original inventory counts or claim
    new adapters are migrated C.

The two new Zig production-use paths are transitional migration debt; report
actual source/line changes including them. Native callback policy has an actual
C consumer only when this explicit selection passes independent acceptance.
Default production replacements remain zero. Device-loss and successful pipeline
request ownership remain untouched, even though they share the accepted queue.

## Build selection and archive identity

Proposed option:
`-Dexperimental-c-callbacks-archive=/absolute/path/libdoe_callbacks.a`.
It is admitted only alongside the existing
`-Dexperimental-c-futures-archive=/absolute/path/libdoe_futures_zig.a`.
Both archives must exist, be absolute, and target the accepted native Linux
x86-64 build. Omitted callback option selects original payload ownership. Omitted
both selects original Zig futures AND payloads with no C includes or symbols.
Callback selection without futures selection is an explicit build error.
The existing futures-only selected build remains supported and its queue behavior
is unchanged. Non-native targets, browser Wasm and generic retained native suites
retain the existing explicit selected-build rejection, not runtime fallback.

Build `libdoe_callbacks.a` with existing GCC C11, PIC, strict warnings/Werror and
pthread-compatible flags. It contains callback-owner object code only. It calls
the accepted public futures API and does not copy/link futures object files into
that archive. Final artifacts link ONE accepted future implementation/domain.
The callback public header is the only new C header required by its typed adapter;
private event size/layout is never exposed to Zig. Archive/header compatibility
must be source-bound to the exact build. No tool download, install or Zig-as-C-
compiler invocation belongs to the C path. Existing retained backend stubs may
still be compiled by the unchanged Zig build and must be identified separately.

Add `test-c-callbacks-integration`, requiring both explicit archive flags, with a
focused test name filter. Keep `test-c-futures-integration` available with either
selected combination and run its existing eleven adapted cases in both. The
headless `dropin` shared library is the actual native artifact under acceptance;
compute/full closure wiring does not establish their executed qualification.
Default/selected artifacts, caches and receipts use different paths. Reversibility
includes symbol audits and actual native consumption, not a source-text toggle.

## Seven real callback families and their retained obligations

1. RequestAdapter: native lifecycle lines593–617. AdapterReply owns callback info,
   status, adapter result and static message view. A null foreign callback releases
   a prepared adapter; pre-transfer failure releases it before native panic.
2. RequestDevice: same owner lines657–686. DeviceReply similarly owns prepared
   device transfer/null-callback cleanup. Its queue is the adapter's instance.
3. BufferMapAsync: buffer owner lines302–359. MapReply retains buffer, status and
   generation. Delivery rechecks destruction/generation/device destruction, emits
   Aborted when required, invokes callback then releases buffer. Allocation failure
   releases its acquired lease but does not reverse prior map-state mutation.
4. Owned QueueOnSubmittedWorkDone: queue owner lines339–382. QueueReply retains
   queue; status is established by existing native flush. Delivery invokes callback
   then releases queue, potentially releasing internal device ownership. The
   no-instance global registry is a different owner, not this new C payload path.
5. Compute/render pipeline early errors: ext_a exports lines157–223. Typed Reply
   retains internal device and immutable diagnostic slice. It delivers null pipeline
   then releases device. Successful request snapshots, in-flight join and loss
   state do not migrate. This family includes two typed public API instantiations.
6. PopErrorScope: ext_a exports lines253–287. ScopeReply owns diagnostic bytes
   inline with length and internal device lease. Copy the entire typed value, not
   a borrowed pointer into its former stack buffer. Scope already popped before
   publication. Deliverer retains exact status/errorType and release order.
7. CompilationInfo: ext_b lines184–237. CompilationReply owns module lease and
   callback info. Its deliverer constructs callback-local info/message ABI records
   borrowing the retained module's message, invokes callback, then releases module.
   C must not flatten these local ABI structures or outlive their callback scope.

Foreign callback userdata remains caller-owned through delivery. C never guesses
which fields are resource handles, clones slice referents, alters typed API
statuses, adds a generic resource release table, or skips cleanup for null foreign
callbacks. Retained API pre-transfer catch blocks keep their original error names
and releases. A returned publication failure does not claim full API rollback.

## Real headless native evidence, not a fabricated backend

In a FRESH Linux process, set the existing `DOE_BACKEND=metal` before any adapter
request. Backend selection is cached. Load the actual default or selected
headless shared artifact and call exported wgpuCreateInstance followed by
wgpuInstanceRequestAdapter. Its actual native implementation reaches the existing
Linux `metal_bridge_stubs.c` default-device creation, which returns NULL; the
existing constructor returns AdapterUnavailable before adapter allocation or
Vulkan probing. Expected result is RequestAdapter status3 (Unavailable), NULL
adapter and exact diagnostic `metal default device unavailable`.

This path really owns a native instance and allocates/registers/delivers/frees its
AdapterReply through the selected payload owner. Exercise immediate0/spontaneous3,
WaitAnyOnly1 and ProcessEvents2, callback-created follower requests, duplicate
waits, self/other-thread wait where legal, and final-caller release with no observer
lease hiding destruction. Default comparison freezes actual observed statuses
rather than inventing them. A missing existing loader is a build/load blocker,
not authorization to install or synthesize a device. This is actual failure-path
ownership evidence; it is NOT successful adapter/device/backend/GPU execution.

Additional real native cases with no instance are explicitly ownerless:

- AdapterRequestDevice(NULL): Error3, NULL device, `invalid adapter handle`.
- BufferMapAsync(NULL): Error3, `invalid buffer mapping`.
- DevicePopErrorScope(NULL): Error3, NoError1, owned `no error scope to pop` bytes.
- ShaderModuleGetCompilationInfo(NULL): CallbackCancelled2, NULL info.
- Compute/render async with NULL device and a structurally invalid descriptor
  rejected before snapshot allocation: ValidationError3, NULL pipeline, actual
  `InvalidDescriptor`/`UnsupportedDescriptor` diagnostic. Supply valid memory with
  an invalid count/pointer combination, never a dangling pointer.

Only modes0/3 use these allocation-free ownerless delivery paths. Modes1/2 reject
under current native fail-fast wrappers; subprocesses establish errors safely.
Null-handle QueueWorkDone routes through the other legacy registry, so is NOT new
payload-owner evidence. None of these null-handle cases proves event allocation,
instance lifetime, deferred owned resource semantics or successful backend use.

## Worker and regression boundary

The actual Metal publisher uses the existing task pool: one Job holds the opaque
token. Failed submit has not queued a job, so mark-ready fallback is exactly once.
Queued interval owns the event/instance; the typed QueueReply separately owns
queue/device. Shutdown drains and joins; it does not abandon submitted tokens.
A real native instance + actual delivery.publish + retained task-pool job can
check CPU transport lifetime and failure fallback without a device. Label it
adapted owner integration, not physical QueueOnSubmittedWorkDone success.

Preserve original callback owner tests, six original future-owner tests, standalone
C queue acceptance, accepted native instance consumer/setup/fatal-cleanup tests,
and all eleven selected integration cases. Keep adjacent mapping-generation,
shader diagnostics, queue global-registry/reentry, task-pool rollback/shutdown,
async-pipeline request cleanup, device-loss and native lifecycle fixtures. Run
focused affected checks; unrun adjacent/physical suites remain explicitly unrun,
not implicitly passed. Do not replace assertions with a second model or relax
qualifications after failures. The independent evaluator freezes its own cases.

Default and selected contracts must additionally prove copied allocator identity,
over-alignment/overflow, failure-before-ID/reference, inline owned messages,
publication/final-reference reentry, uncertain-status termination and no reads
after synchronous free. Retain initial failures separately from repairs and name
which C versus retained Zig code each sanitizer actually instruments. Preserve
physical fixtures and their independent GPU requirements unchanged.
