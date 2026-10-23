# C label owner, ABI 1

Status: source/header contract frozen after design review and the explicit cleanup
precedence correction. CONTRACT.sha256 seals this installed contract-only snapshot;
no implementation or executable tests exist. Independently authored cases MUST be
sealed before implementation or executable test realization. The header, seam,
source inputs and author evaluation requirements form one review packet. Shared
route/build inputs remain candidate3-bound and unaccepted, not accepted producers.
Any later change requires an explicit source-drift delta and reseal.

## Bounded owner and baseline

Baseline checkout HEAD da9b75a09e93248f52749d5aa7dd414f4d8a0016 on
experimental/c-migration-dot-20261023. Published guide reference supplied by lead:
03d7a5cc311c46f0fa5df01f4e3383d126e3253c (not a remote-verification claim).
The M12 front-door documentation-only rebind is recorded in SOURCE_REBIND.json.
Actual retained owner: runtime/zig/src/native/support/doe_label_store.zig,
SHA-256 242c2172d17015277fdd1ee19c75551bff9ea1760e80f8b5539a78320dce782d.
Its 71 physical lines own one synchronized pointer-address map, byte copies and
three C ABI exports. Source inventory and full callsites are in SOURCE_INPUTS.json
and SOURCE_TRACE.md. MIT attribution and upstream notices remain unchanged.

C replaces only label allocation, key lookup, replacement and removal. It does
not own object counts, provider selection, object validation, callbacks/futures,
error scopes, native rendering, instance destruction or GPU completion. Labels
are arbitrary copied bytes, not text, UTF-8, C strings or interned values. No new
registry framework, object shell, reference count, callback mechanism or owned-copy
read API is introduced. No actual consumer presently requires an owned-copy read.
Standalone C11/POSIX Linux x86-64 and strict C++17 static-archive callers are the
initial envelope. The standalone archive invokes/links no Zig. The separate
retained-consumer seam is explicitly mixed-language and default-off.

## API and external lifetime

The header is authoritative for record members, enum values and prototypes.
Each successful create returns one independently owned store. The copied allocator
binding owns the control allocation, table and all label allocations. No hidden
fallback allocator is used by this API. Stores never share entries, locks or
allocator state implicitly. The same key in two stores has unrelated values.

Keys are opaque pointer tokens converted to uintptr_t, never dereferenced,
classified, retained or validated as GPU objects. NULL is the missing key. Live
object callers must keep their existing external object lifetime and call remove
before reuse/free. A query using a preserved integer key after release is a map
query only, not permission to call an object operation through a stale pointer.
Pointer-key reuse has no generation magic: after remove, a reused address starts
absent. A missing remove is a lifecycle bug, not something this map can infer.

Input config/output storage is valid, properly aligned and nonoverlapping for
the full call. A non-no-op set requires length readable source bytes through
copy. Outputs may not overlap store-owned storage. Invalid non-NULL pointers
are caller violations, not safe rejection inputs. A source may alias an existing
label if caller exclusion keeps that source alive through the call; the new copy
is made before the previous allocation can be freed. No label length cap or
implicit SIZE_MAX/NUL-terminated string convention is added. Upstream string-view
normalization, where needed, belongs to its existing caller.

A get view remains valid until that key is successfully replaced, removed or the
store is destroyed. Successful no-op set and failed replacement preserve both
bytes AND the old pointer. Mutations of other keys, including table growth, do
not move label allocations. The internal lock is released before get returns.
It does NOT make subsequent dereference safe across concurrent replacement or
remove. Callers must exclude same-key invalidation through their entire use;
holding the object alive alone does not prevent another caller's set.

Set/get/remove serialize map state with one mutex. Shared-store mutation is
supported under external store/object lifetime. Concurrent readers may dereference
only with the stated exclusion, or on distinct keys whose ownership excludes
invalidation. Destroy requires complete external exclusion. No operation admits
asynchronous cancellation, signal-handler use, longjmp, exceptions through C,
fork-with-active-threads or allocator reentry into any label-store operation.

## Explicit failure and allocation

Create validates out first, initializes *out=NULL, checks config and ABI version
before nested fields, then both allocator hooks. It copies the config, allocates
the control record, initializes exactly one mutex and publishes only on success.
Control allocation failure returns OUT_OF_MEMORY. Mutex-init failure returns
SYNCHRONIZATION_INIT and frees the control record without destroying a mutex
whose initialization failed. No earlier resource requires fallible rollback.
Allocator state/vtable/hooks remain live until destroy succeeds.

Set first rejects NULL store as INVALID_ARGUMENT. A valid closing store then
returns CLEANUP_REQUIRED, before examining the NULL-key/NULL-data/length-zero
no-op branches, without locking, allocating, calling hooks or reading source data.
Only an active store preserves those no-op branches as OK without a lock or hook.
Remove uses the same precedence: NULL store, closing store, then NULL/absent key.
Get checks NULL out first (INVALID_ARGUMENT without inspecting store), then NULL
store (INVALID_ARGUMENT), then closing state (CLEANUP_REQUIRED with output unchanged).
Thus closing-state rejection-only set/get/remove probes are defined and inert;
they do not recover storage or authorize any ordinary operation. For a nonempty write it privately
prepares the new byte copy and any required replacement table before publishing.
Every size multiply, addition and capacity expansion is checked before allocation
or pointer formation. Layout/capacity overflow is LAYOUT_OVERFLOW; NULL allocation
is OUT_OF_MEMORY. On either, all temporary allocations are freed, no prior entry
or borrowed pointer changes, and no partial table/value becomes visible. No
arbitrary new fixed capacity, truncated key/length or success-on-failure is allowed.
Exact allocation request pointer/extent/alignment/state/vtable pairs match free.

A small typed entry {uintptr_t key; byte pointer; size_t length} and private
open-addressed table is the proposed implementation, with separate label bytes.
No generic macro/container abstraction is warranted. Collision and deletion logic
must be independently tested; growth policy is private, not a semantic contract.
One cohesive store owner and a separate native-singleton binding suffice.

Allocator calls during mutation occur under the map mutex, matching the actual
retained source's allocation serialization. This is an explicit narrow exception
to the usual no-foreign-callback-under-lock guideline, not permission for user
callbacks, loader operations or reentry. Hook state must support any admitted
calling thread, including other independent stores, and deallocation need not be
on the allocation thread. Allocator invalid/misaligned/undersized returns are
contract violations; any diagnosable alignment failure terminates explicitly.

Remove frees a matching label once without allocation. Table storage may remain
until store destruction, matching retained amortized storage behavior; no leak is
excused by that policy. Get allocates nothing. On invalid public arguments or
CLEANUP_REQUIRED, get leaves its output unchanged. Recoverable statuses must be
handled by every direct C caller, not ignored as an assumed success.

Destroy(NULL) succeeds. Exclusive destroy first marks the store closing and
attempts mutex destruction before freeing any data. If destruction reports failure,
return CLEANUP_REQUIRED and retain control/table/labels/allocator and mutex
obligations. Rejection-only ordinary probes remain defined under the validation
precedence above, without a mutex operation or allocator hook. Only an exclusive
destroy retry can recover and release this storage. A failed POSIX destroy leaves
its mutex unmodified; wrappers must respect that when injecting a recoverable error. A successful retry frees
all allocations. This is not concurrent destruction or implicit leak recovery.
Unexpected lock/unlock errors are not ordinary statuses: because synchronization
or publication may be uncertain, terminate with operation/code diagnostics and
never pretend a recoverable successful mutation. These paths require subprocess
negative/fault evidence, not a hidden panic or ignored pthread return.

## Known retained gaps, not silently expanded claims

The accepted instance cleanup callback removes labels after queue teardown and
before object free. It must reach the SAME selected store as exported setters.
All observed calls are enumerated in SOURCE_TRACE.md, including shared fence and
shared texture-memory owners outside native/.

Real wgpuSurfaceSetLabel writes this map, but doeNativeSurfaceRelease currently
destroys the surface without remove. That is an inherited cleanup/pointer-reuse
bug, not safe lifetime semantics. Freeze a CPU unconfigured-surface regression
before a coordinated, provider-independent one-call lifecycle repair. Until it is
fixed in both controls, surface-release cleanup cannot be claimed. The existing
accepted instance test labels the instance, not the surface, so does not cover it.

wgpuExternalTextureSetLabel delegates to a separate retained no-op after cast;
wgpuSharedBufferMemorySetLabel is also a no-op. Those are excluded from label
owner migration, not retroactively called implemented. Generic SetLabel can label
an external-texture token, but its finalizer currently lacks generic-map remove;
record that separate cleanup gap rather than assert arbitrary object release
cleans arbitrary labels. Native object helpers are not a universal destructor.
The package device label getter/setter are stubs; the real N-API objectSetLabel
and request-device/default-queue descriptor paths are traced but package adoption
is not established by a C/native CPU result.

## Acceptance and scope

Independent tests bind this contract/header, real source hashes, candidate and
producer identities before execution; preserve first failure and repair evidence.
Actual C/C++ archive consumption, native ABI identity, singleton/provider isolation,
real instance cleanup and the separately repaired surface case are mandatory.
C-only sanitizer success does not instrument retained Zig allocators/refcounts.
No GPU/backend/device-success, package/browser adoption, performance, production
replacement or 50% size claim follows from this bounded owner. Charge C/header/
private binding, build verification and transitional Zig glue in the quality
closure. Do not manipulate formatting or hide required code to meet a size goal.
