# Real retained-consumer C-label seam

## One selected owner, unchanged exported contract

Add an explicit existing absolute external GCC PIC archive option:
-Dexperimental-c-labels-archive=/absolute/libdoe_labels_native.a.
Its omission is the retained Zig owner, with no C label import/link/setup. Invalid,
missing, wrong-target, stale or mismatched archives fail closed before link; never
compile a fallback or select C merely because an archive happens to exist.
Initial envelope is native Linux x86-64. Standalone labels do not depend on C
futures/callbacks/instance; the native option must compose independently with them
and with the separately selected drop-in-policy option. Non-native/WASM selection
rejects explicitly. Production/default selection remains unchanged.

Proposed exact consumer edits, after contract/cases seal and lead coordination:
1. New native/support/doe_label_owner.zig selects retained doe_label_store.zig or
   new doe_label_c_adapter.zig at compile time and aliases set/get/remove and the
   three exported ABI functions. It owns no state or policy.
2. Replace the two direct imports in native/support/doe_native_object_helpers.zig
   and native/api/doe_native_api_misc_exports.zig with that same owner facade.
   native/mod.zig already re-exports the misc facade, so all creation/lifecycle,
   exported object setters and browser shared-memory calls converge on one owner.
3. The C adapter imports only doe_labels_native_private.h, maps typed byte spans
   and exports exactly existing doeNativeObjectSetLabel(handle,data,len),
   doeNativeObjectGetLabel(handle,out_len), doeNativeObjectRemoveLabel(handle).
   No WebGPU procedure name or public ABI record changes. get() preserves the
   existing []const u8 interface for any native caller; zero maps to empty slice.
4. Original doe_label_store.zig is byte-for-byte retained and not imported in
   selected builds. No simultaneous second map; no copying old labels into C.
   Provider selection is fixed for each loaded library, never toggled live.

The old void SetLabel silently preserves the old label on allocation failure.
The adapter must explicitly switch statuses: OK returns; OUT_OF_MEMORY and
LAYOUT_OVERFLOW return without changing the old value. Unexpected statuses from
this valid singleton binding terminate with operation/status diagnostics, not a
silent no-op. GetLabel writes zero to a non-NULL out_len when absent, returns NULL
for absence, and otherwise returns the exact borrowed pointer/length. NULL out_len
is supported. Remove remains void and idempotent. No new get-copy ownership or
string-termination convention is smuggled into these existing symbols.

## Native singleton allocation, setup and destruction

Proposed private integration unit src/native.c owns exactly one static store for
that artifact, with PTHREAD_MUTEX_INITIALIZER, empty table and an explicit libc
malloc/free allocator. It uses the same typed store algorithms as the public API.
There is no lazy allocated control record, fallible first-use initialization,
pthread_once failure, process-wide external registry or hidden Zig allocator.
The native singleton intentionally switches internal label allocation from the
retained Zig GPA to C libc, not object allocations; this must appear in evidence.
All requested types have alignment no greater than max_align_t; label bytes use
byte alignment. This native allocator binding owns/frees all its table/label bytes.

A private ELF/GCC destructor in native.c performs exclusive shutdown at quiescent
DSO unload or normal process termination. This host integration extension is NOT
required by the standalone C11 store archive. It destroys the static mutex and
frees every remaining label and table without freeing the static control record.
No public teardown/test-mutation entry point is exported. Mutex destruction failure
at this irreversible unload boundary emits diagnostics and aborts before freeing
still-owned state; it cannot return a fake successful unload or silently leak.
The standalone API instead exposes recoverable exclusive destroy/retry as stated.

The embedder must quiesce every API call, object operation and borrowed label use
before unloading; shutdown cannot create that quiescence or make outstanding
foreign objects safe. Repeated dlopen/dlclose tests use genuinely unloaded modules,
not RTLD_NODELETE aliases. No API call from a later teardown callback is admitted
after this owner has shut down. Exact destructor/link retention is verified in
the real native artifact. The reviewed native destructor choice is part of this frozen seam; if unavailable
on a target, the selected target must
reject rather than quietly adopt process-lifetime heap leakage.

Standalone stores remain separately allocated. Native static and standalone
control records share one private typed record and internal destroy-contents
helper, with an explicit ownership flag/parameter only where free differs. Do not
copy the whole map implementation or genericize independent resource lifetimes.

## Singleton isolation and symbols

Compile every C owner function with hidden ELF visibility for the native PIC
archive, including the ordinary public C functions; static C/C++ callers can still
link the separately built core archive. No C-only DSO API is promised in ABI 1.
Native private functions and state are local/hidden, never weak or preemptible.
The native DSO adds no public label symbol beyond the existing three exports.
Probe nm/readelf/relocations plus two actually loaded DSOs: assigning the same
opaque token in A and B must give independent bytes and removal/unload in A must
not affect B. Test loader namespace/interposition behavior, not merely two stores
inside one executable. No provider/device identity is inferred from raw keys.
This owner does not repair existing fallback-setter routing outside its map.

## Existing C-instance callback and lifecycle repair

instanceRemoveLabel in native/lifecycle/doe_instance_device_native.zig:524–527
calls native_helpers.label_store.remove. C instance src/instance.c:finalize calls
that hook only after successful real future-queue deinit and before clearing magic
and object deallocate. Neither callback signature, ordering, native allocation
trace identity nor the instance refcount changes. The facade import replacement
makes this existing callback consume C labels with no new C-instance dependency.
Its dying-instance key is not dereferenced by the label store.

Separate inherited defect repair, not implemented in this packet:
- File: runtime/zig/src/native/surface/doe_surface_native.zig.
- Existing native_helpers import at line 22 is sufficient.
- In doeNativeSurfaceRelease after release_surface_instance(surf), immediately
  before surface_allocator().destroy(surf), add
  native_helpers.label_store.remove(surf_raw).
- The existing successful final-reference guard still dominates this call.
  All backend/resource/device/instance cleanup ordering stays unchanged; no new
  dependency/import, GPU call, descriptor behavior or refcount is introduced.
- Seal an independent actual unconfigured-surface regression first. Record its
  failure on the old source, then fix/default pass, then selected pass. Do not
  rewrite accepted instance receipts or call their old coverage comprehensive.

## Shared build and routing files: coordination required

DO NOT edit during the active compiler/drop-in-policy evaluation. Current shared
route/build inputs are source-bound candidate3 observations, explicitly unaccepted.
Only the accepted M12 front-door documentation delta has been rebound; it does not
qualify route producers. Lead integrates once source identities and exact patch
window are agreed:
- runtime/zig/build.zig: add explicit validated archive option, boolean to every
  applicable ordinary/compute/full build_options record, include/link helper and
  selected integration step. Apply to every real native module/artifact and any
  concurrently added selected suite, without coupling labels to futures. Add
  unsupported-target/WASM guard and source/receipt verification dependency to
  selected ordinary artifacts, not only the special test step.
- runtime/zig/source-layout.json: register facade/adapter ownership and consumer
  disposition, update touched hashes/reasons without weakening import rules.
- runtime/zig/src/README.md: regenerate from its canonical producer if required.
- config/zig-test-inventory.json and its canonical generator: register selected
  tests/root under existing rules, preserving all old tests and counts.
- runtime/zig/test_suite_c_labels.zig and tests/c_labels_integration_test.zig:
  new test-only selected root/consumer tests after independent case seal.
- runtime/c/labels/: prospective child charter, README, STYLE, CONTRACT, headers,
  src/store.c/internal.h/native.c, Makefile, explicit archive producer/receipt
  verification, source provenance and independent test directory.
- runtime/zig/CATSCAN.md, docs/c-migration/MIGRATION.md, docs/component-index.md
  and affected generated inventories only when actual accepted boundary changes
  justify updates. No accepted-production count changes from a draft or CPU pass.

Do not modify futures, callbacks or instance implementation/contract just to route
labels. Keep their historical source manifests immutable; new native comparisons
bind current shared sources separately. No compiler/archive producer edits are
needed for the standalone label implementation. Every added build/verifier/helper
is charged in the quality delivery closure; no registry framework is warranted.
