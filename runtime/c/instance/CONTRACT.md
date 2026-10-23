# C-owned native instance, ABI 1

Status: frozen specification prepared for final exact-seal review and lead
installation on 2026-10-23. Independent design review found no architectural
blocker; its two precision fixes are incorporated. The callback prerequisite is
closed at accepted commit `3582e86472db945cc441eedb3796ec7f1b13d179`.
No instance implementation or acceptance is claimed. Independent evaluator cases
must be separately authored and sealed before candidate implementation. This
package contains only contract/header/acceptance inputs, not runtime source edits.
Compiler work, including the active M10 files, is a separate owner.

## Decision

Use one opaque C instance allocation containing one C atomic reference count and
storage for the accepted C future queue. Native holders keep that pointer, not a
Zig shell. Replace the selected future adapter's pointer-to-wrapper owner with a
copied, non-owning queue-plus-lease value. Preserve the original Zig owners and
all existing selections; this new instance selection requires both accepted C
futures and admitted C callbacks. Qualify actual instance creation, unavailable
adapter delivery and real unconfigured Linux surface retention, not GPU success.

This is the next bounded ownership move, not a five-wrapper port. It includes
all accesses to the actual instance count, identity, queue, allocation and final
cleanup. Device/backend selection, external-texture registry policy, typed
callback resources and the legacy global work-done registry remain their owners.

## Bound source and accepted predecessor

Accepted predecessor: `3582e86472db945cc441eedb3796ec7f1b13d179`.
The original campaign base remains `665475907494d2095a760596218b4c2d363cb436`.
[SOURCE_INPUTS.json](SOURCE_INPUTS.json) and [SOURCE_INPUTS.sha256](SOURCE_INPUTS.sha256)
bind the reviewed 103-file trace, 14 additional dependencies and accepted callback
receipts to that commit and current matching worktree bytes. All runtime/build/ABI
inputs from the reviewed trace are unchanged; its three changes are acceptance
prose in MIGRATION.md and callbacks/CATSCAN.md and README.md. Existing drafts,
seals and source snapshots remain intact; no historical receipt is rewritten.

[Provenance](PROVENANCE.md) records derivation and history. The accepted callback
[report](../callbacks/independent_tests/REPORT.md) closes its fresh canonical
futures-only DSO/control gate. This source binding does not transfer those tests
to the new instance owner. The [contract seal](CONTRACT.sha256) binds this exact
specification, header, retained seam and author acceptance requirements.

Owner instructions read: AGENTS, GOALS, INTENT; root/runtime/C/ futures/callbacks/
Zig charters; applicable Zig/native-adapter and futures style; runtime map/test
instructions; architecture's native ownership boundary; callback and device
lifecycle contracts; charter, verification, refactor and evidence-retention law.

## Exact C header surface

Authoritative file: [include/doe_instance.h](include/doe_instance.h). C11 and C++17 compatible,
using the existing pinned `webgpu.h` and accepted opaque `doe_futures.h`. No Zig
headers, no handwritten replicas of WGPU records, no C dependency on callbacks.
The native build nevertheless requires C callbacks, to avoid making the retained
payload implementation understand the new opaque native type.

The header contains the frozen declarations. It is the single declaration source;
this contract owns their semantics. The header is not a runtime implementation.

These are private experimental-owner entrypoints in the selected native artifact,
not changes to existing wgpu*/doeNative* signatures or a new provider registry.
Null retain/release/undo are no-ops; borrow_queue requires a live non-null instance.
C++ sees only C-compatible opaque handles and records, never `_Atomic` internals.
All object calls require externally established lifetime, as the original handles
do. The copied configuration's allocator/hook targets must remain valid, be
thread-safe for their calls and return normally through final destruction.
`state` may be null for a stateless allocator, as in the admitted callback seam.
Both allocator function pairs are mandatory. Optional observer/label hooks mean
no-op; optional external count means zero. Native integration supplies all three.
No hook acquires an implicit reference. No foreign hook runs under a queue lock.
The allocated hook is observation-only for an unpublished, uninitialized object:
it cannot retain, borrow or reenter instance APIs. remove_label runs after the
count has reached zero and queue teardown succeeds; its instance pointer is a
cleanup identity only, not a usable lease. It cannot retain, borrow, resurrect
or reenter instance APIs. The deallocation hook likewise cannot resurrect or
reenter the dying instance. These restrictions do not limit ordinary delivery
callback reentry while its real event/full-call leases remain live.

## Allocation, identity and finalization

1. Validation precedence is exact: if out is NULL, return INVALID_ARGUMENT
   without reading config. Otherwise write *out = NULL; reject a NULL config;
   read abi_version and reject an unsupported version before reading nested
   allocator/hook fields. Then validate both allocator function pairs, snapshot
   the configuration by value, and begin fallible work. Null state/vtable tokens
   are allowed when their allocator treats them as unused. Config and out must
   be valid nonoverlapping storage through return. Query accepted
   `doe_future_queue_requirements` for version 1. Compute a
   checked aligned queue offset after the private C control and total extent;
   use max(control alignment, queue alignment). Check align-up/addition and
   power-of-two assumptions before mutation. No guessed queue byte array or
   inclusion of its private internal layout is needed.
2. Obtain one untyped allocation from object_allocator with exact extent/alignment.
   The control prefix and disjoint trailing queue storage acquire their own C
   effective types through typed initialization and the accepted queue-init API.
   Initialized storage is never moved or copied. The private control stores the
   sole `_Atomic uint32_t` count, queue pointer, original allocator bindings,
   cleanup hooks and exact allocation extent/alignment.
3. Invoke allocated exactly once immediately after successful allocation, before
   queue initialization, matching current make/create observation timing. The
   pointer is observation-only and unpublished; this hook cannot call object APIs
   or retain it. Trace actual allocation extent, including queue/padding, not
   sizeof an opaque pointer or an old Zig object. Initialization failure can thus
   have a creation-observation row without a published handle, as before.
4. Initialize count to one, explicit queue and header state, then publish only
   after queue init succeeds. Queue ordinary init failure has completed its own
   rollback: free the single allocation and return QUEUE_INITIALIZATION/NULL.
   Allocation/checked-layout failures return the matching status/NULL, not panic.
5. CLEANUP_REQUIRED on queue init is diagnostic termination, without freeing live
   synchronization. The public native API has no cleanup-only object channel.
   Do not retry setup, claim rollback, leak-and-return success, or expose a usable
   instance. Standalone futures' separate exclusive cleanup retry API is unchanged.
6. C retain uses relaxed load/CAS, rejects zero and UINT32_MAX before increment,
   and never resurrects or wraps. Ordinary release performs the existing
   acquire/release decrement and exactly one final-owner decision. Releasing a
   zero count terminates diagnostically. This does not make concurrent use after
   the final owning lease or arbitrary stale pointers valid.
7. Every ordinary release first invokes the existing external_texture_count hook
   for the same pointer key. When positive, use the existing CAS behavior: reduce
   only while count > 1, with acq_rel success and relaxed load/failure, otherwise
   preserve one and return. Do not replace this guard with a second C texture
   count, a generic registry, a weak reference, or unconditional destruction.
8. `undo_retain` is a DIFFERENT operation: a non-final acq_rel decrement of the
   speculative retain before external-texture registration. It ignores the
   registry, requires an independent original owner and rejects count <= 1.
   The two existing allocation-rollback sites use it. It cannot finalize, and
   normal release is not silently substituted for it.
9. On ordinary final release, require accepted queue deinit OK, then remove label,
   invalidate private magic and deallocate with the captured original allocator,
   pointer, total extent and alignment. Snapshot every deallocation input before
   free and never reread control afterward. Pending/active/BUSY, cleanup-required
   or other unexpected statuses terminate with operation/status diagnostics;
   none permits label removal/free, false success, or ordinary rollback.

The opaque C control's first member is the existing instance magic value
0xD0E10001, deliberately defined by C, not inferred from the old Zig layout.
from_handle uses memcpy to read that C prefix only from a non-null live/readable
handle. A mismatch returns NULL; a matching authentic selected instance yields
its C identity. No type-punning of the old non-extern DoeInstance or other Zig
object is allowed. Like the old magic check, this is not authentication of
arbitrary forged storage, inaccessible memory or stale pointers. Negative tests
use sufficiently readable storage with an actual nonmatching marker and genuine
live wrong-kind objects; never random addresses. Do not use an old-instance
layout probe to recognize a new instance or permit live objects to change owner.

## Stable queue and lease, without a Zig instance shell

`doe_instance_borrow_queue` returns a plain NON-OWNING value containing the actual
C queue pointer, the instance pointer as userdata, and C retain/release thunks.
Copying it does not retain. A caller already holds a lease covering its use and
must acquire another lease explicitly before it escapes that scope. No temporary
view address is stored, no queue/pthread storage is copied, no second future
namespace is introduced. It is valid only while that exact instance is live.

Amend `doe_future_c_adapter.zig` narrowly:

- Introduce `QueueView { queue_ptr: *c.doe_future_queue, lease: ?Lease }`, with
  newFuture/register/borrowedQueue/waitAny/processEvents methods operating on the
  pointer. View copying is explicitly non-owning. Null lease remains restricted
  to explicitly initialized, externally quiesced standalone fixtures.
- Keep PendingCompletions' existing owned C storage, initialization/deinit and
  layout proof for the two older selected-native combinations. Its methods
  delegate via a value returned by view(). This preserves existing consumers
  without an extra queue, shell or C-instance allocation in those combinations.
- Change selected `Completion.owner` from `?*PendingCompletions` to `?QueueView`.
  Registration snapshots the complete value before a producer can publish. It
  does not acquire an event lease: the real request/event remains responsible.
- markReady snapshots owner/lease before entry, takes a full-call lease, calls
  accepted C readiness and releases only after return. Do not read self afterward.
  Trampoline still reads dispatch at delivery time, not registration; pipeline
  dispatch is installed late. finish retires, deinits, clears local owner before
  payload destruction. Never-initialized finish remains harmless.
- QueueView wait/pump snapshot the value and hold a full-call lease, even when
  used directly rather than through native exports. Existing old-instance wait
  conversion remains typed and checked; do not duplicate scheduler policy.
- `doe_future_owner.zig` adds `QueueRef`, selected-C QueueView versus retained
  `*PendingCompletions`, and a borrow(pending) adapter. Original
  `doe_future_ids.zig` remains byte-identical. Existing default inline tests
  continue to use their original retained owner. Adapted selected tests are named.

New `doe_instance_c_adapter.zig` imports the authoritative C header, exposes its
opaque Instance type and typed adapters, verifies the existing exact GCC future
bridge layout before publishing a selected native instance, and has no import of object types,
helpers, lifecycle or external-texture policy. This keeps dependency direction
acyclic. `doe_native_object_types.zig` selects that opaque Instance only for the
new flag, otherwise retains the current DoeInstance struct body unchanged.
`doe_native_object_helpers.zig` specializes instance cast/add-ref only under that
flag, and adds typed instance_pending/instance_undo_retain accessors. Its generic
object_should_destroy must reject use for the opaque selected instance at
compile time: C alone may decide and perform its finalization. Other objects'
reference policy does not change. Existing raw pointer identity is the key used
by adapters, surfaces, external textures, labels and callbacks.

## Native wait/pump and capability semantics

The new C owner implements WGPU wait record adaptation using the actual pinned
WGPUFutureWaitInfo type. Its count-zero path succeeds before reading instance or
infos, including NULL/zero-time and NULL/infinite-time cases. For nonzero count,
NULL instance returns Error3. Valid storage for count records is a precondition,
except checked multiplication must reject overflow before reading any record.

Take one full-call C instance lease; checked-allocate accepted C wait records via
wait_allocator; copy every input completed flag and ID; call accepted queue;
copy all resulting flags back, free temporary storage, then release the lease.
OOM/size overflow returns Error3 without reading/modifying flags. Queue OK maps
to Success1, TIMED_OUT to TimedOut2, all other fallible wait statuses to Error3,
matching the current selected bridge. Do not split waits, treat timeout as
retirement, drop duplicate records, change same-thread self-wait or deadlines. A pump defers identities issued after
its own captured boundary; an explicit recursive ProcessEvents call captures a
fresh boundary and may deliver those identities. Do not suppress that admitted
recursive delivery merely because the outer pump will defer them.
All cleanup inputs survive a callback that releases the last caller reference.

Native adapter binds object_allocator to native_helpers.alloc and wait_allocator
to the existing page allocator, using copied state/vtable identity and raw
aligned allocate/free thunks. Test overrides are test-only. WGPUFuture and
WGPUFutureWaitInfo already come from generated/webgpu_upstream.zig's import of
this pinned header; freeze that provenance and full ABI field/enum/call probes.
Do not cast an unrelated doe_future_wait_info as a WGPU record. The C owner owns
wait allocation/status/lifetime sequencing; Zig is not a second wait owner.

Standalone C process_events(NULL) is a no-op. For a live instance, C
process_events holds a full-call reference through accepted queue return. The
native wrapper recognizes its handle before this C call; its existing
invalid/null ProcessEvents branch still drains
queue/doe_queue_lifecycle.zig's global work-done registry. It is not converted
into C instance state. Native null/readable-invalid AddRef/Release remain no-ops;
nonzero invalid WaitAny returns Error3. Native CreateInstance continues to ignore
the descriptor and maps ordinary C create failures to NULL.

C returns empty instance features (count0, pointer NULL), false for ALL feature
values, and limits with nextInChain NULL and timedWaitAnyMaxCount0. The native
limits export still returns Success1. FreeMembers remains a no-op. Adapter
fieldwise copies the imported C capability records into the existing Zig extern
records; it does not overlay unrelated pointer types. No TimedWaitAny promise
is inferred from implemented waits. WGSL-language features remain unchanged.

## Real consumers and required integration edits

Paths below are relative to runtime/zig/src. No general device/backend rewrite.

- lifecycle/doe_instance_device_native.zig: conditional selected C create,
  release/wait/pump; typed instance hooks for external count, labels, observation;
  loss Event.create receives instance_pending value. All real adapter constructors,
  backend selection and device logic otherwise remain unchanged. Adapter retain,
  GetInstance and final release resolve through the selected helper/native exports.
- support/doe_native_object_types.zig and doe_native_object_helpers.zig: exact
  opaque type selection and instance-only dispatch/accessors described above.
- support/doe_callback_c_adapter.zig: consume instance_pending(value) by value,
  copy its exact queue and lease to accepted doe_callback_owner. Original
  doe_callback_delivery.zig and its retained tests remain byte-identical; the new
  instance selection cannot select that allocation implementation.
- support/doe_device_loss.zig: Event.create owner parameter becomes
  ?future_owner.QueueRef. Event's pending obligation still retains instance before
  register and releases only after callback, completion finish and event release.
  Keep its device/reference mutex policy. A copied view supplies no implicit lease.
- dropin/wgpu_dropin_ext_a_pipeline.zig: next_async_future_id and
  register_pipeline_future use instance_pending value. Request retains instance
  before registration; retirement/free precedes its release; late dispatch and
  actual readiness paths remain unchanged.
- resource/doe_external_texture_native.zig: replace the two rollback count
  decrements with instance_undo_retain; both retain sites, registry keys,
  register/deregister ordering, plane cleanup and final native Release remain.
  The silently caught registry-allocation failure is inherited separate debt,
  not a license to change registry policy here. Successful textures remain GPU gates.
- surface/doe_surface_native.zig: generic instance cast/add-ref and native release
  must reach C identity/count. No body rewrite needed if this is proven. Its real
  unconfigured Linux allocation retains instance; explicit non-null invalid device
  binding releases it and frees surface. Actual compile-time has_vulkan closure,
  standard/native entry and destructor must all be exercised. Configured runtime
  equality still compares the same instance pointer; configuration is not qualified.
- dropin/wgpu_dropin_ext_a_exports.zig: selected C feature/limit adapters; current
  native instance recognition for CreateSurface must identify the actual C object,
  never fall through to the resolved incumbent for a genuine selected instance.
  Preserve invalid/foreign-route behavior rather than broadening provider ownership.
- dropin/wgpu_dropin_ext_b.zig: selected feature-free adapter only; no changes to
  unrelated callbacks. Standard/native surface release recognition stays real.
- diagnostics/doe_program_identity_trace.zig: one narrow observation overload for
  an opaque native object and explicit actual allocation size. Share existing row
  writing/sequence/lock behavior; preserve before-init timing and schema. Keep the
  logical instance type name source-bound, and disclose changed physical size.
- Existing mod/API facades, dropin_abi_procs, proc manifest and actual local lookup
  should need no symbol/signature change. Inspect and test them, do not add another
  public proc registry. Any necessary edit outside this list requires review first.

The borrowed queue seam reaches buffer/queue/map/scope/compilation consumers via
callback_owner and retained device->adapter->instance topology. They do not gain
new resource ownership or success semantics. N-API, Bun and pinned Dawn proc-table
consumers retain their public ABI and separate execution qualification gates.

## Reversible build route and files

Declared C owner files: runtime/c/instance/{CATSCAN.md,CONTRACT.md,README.md,
STYLE.md,PROVENANCE.md,Makefile,.gitignore}; include/doe_instance.h;
src/{instance.c,wait.c,capabilities.c,internal.h}; tests/{test_api.c,test_cpp.cpp,
test_faults.c,test_concurrency.c}; integration/{CONTRACT.md,build_archive.sh,
native_consumer.c,abi_probe.c}. No implementation file is created by this contract package.

The instance archive is GCC-only PIC C11 with strict warnings and pthreads.
It contains only the instance owner, not copies of future/callback objects.
Use public futures headers and the existing pinned WebGPU header. Link exactly
one externally built future archive and one callback archive. No hidden Zig C
compilation, generated C, alternate scheduler, tool download or link fallback.

Add `-Dexperimental-c-instance-archive=/absolute/path/libdoe_instance.a` and
`experimental_c_instance` boolean, default false. Presence requires BOTH existing
explicit C-futures and C-callback archives, existing absolute paths, native
Linux x86-64 and admitted callback prerequisite. Missing/unsupported inputs fail
before execution, not switch owner. Existing combinations stay available:

- no C flags: original instance, original futures and original callback payload;
- futures only: original instance + accepted C futures, original callback payload;
- futures+callbacks: original instance + accepted C futures and C callbacks;
- futures+callbacks+instance: C instance + same accepted queue and C payload.

No instance-without-futures, instance-without-callbacks or runtime hot switching.
Extend configureCFuturesModule into a clearly named configured-owner helper,
preserving each existing consumer, and wire the new boolean/include/archive to
main/compute/full modules and every currently configured artifact root. Preserve
explicit non-native browser-wasm rejection and generic retained-suite rejection.
Inventory the actual closure from build.zig, not just the default dropin target.

Add focused `test-c-instance-integration` with a generated
runtime/zig/test_suite_c_instance.zig and tests/c_instance_integration_test.zig
through config/zig-test-inventory.json. Do not pretend the existing selected
fixtures with direct Zig instance fields compile unchanged: keep their old lanes
available, adapt those same obligations into the instance lane using real C
instances and observer/fault hooks, and explicitly reject old selected fixture
steps under the instance flag if they are not generalized. No blanket full-suite
claim. Update source-layout ownership, migration manifest, relevant charters and
integration contracts together under this reviewed specification and the
separately frozen evaluator cases.

## Acceptance requirements and implementation gate

The author acceptance requirements are in [EVALUATION.md](EVALUATION.md); they
are not independently authored evaluator evidence. The design boundary and its
two clarifications have been reviewed without an architectural blocker; callback
acceptance is confirmed. Final exact-seal review and lead installation establish
the installed specification. The independent evaluator must then freeze its own
cases and baseline expectations before implementation; the existing future
integration contract's value-view amendment must be installed with the boundary.
A review objection is a blocker, not authority to silently choose another
architecture or expand the owner.

Future CPU acceptance can establish selected host instance ownership and named
real CPU consumers only. Adapter/device/pipeline/resource success, real external
textures and their final-release guard, configured surfaces/presentation, actual
GPU timeout/loss/unknown completion, Vulkan/Metal/D3D12 hardware, package/proc-table
and browser adoption remain distinct unexecuted gates. No C line count, fake
object, compile-only consumer or CPU simulator closes them.
