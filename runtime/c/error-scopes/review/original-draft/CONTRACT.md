# C error-scope owner, ABI 1: review draft

Status: design only, not frozen, installed, implemented or accepted. All author
artifacts are confined to this directory. Independent review and independently
frozen cases must precede implementation. M12 and dropin-policy work is untouched.
Branch: experimental/c-migration-dot-20261023; observed HEAD:
da9b75a09e93248f52749d5aa7dd414f4d8a0016. SOURCE_INPUTS.json binds exact read bytes,
including explicitly dirty shared integration files. Refresh/review those files
once their current owners release them; this draft cannot freeze someone else's
working changes. Default/main production remains retained Zig.

## Responsibility and one policy owner

C owns the per-device 64-entry stack, first-error capture, filtering, bounded
message copies, synchronized handler tuple, and synchronous dispatch ordering.
One private pthread mutex protects each actual stack. There is no allocator,
queue, future identity, callback payload allocation, device reference count,
active-call reference count, GPU state, cancellation, loader or global registry.
The standalone archive is C11/POSIX, initially Linux x86-64 GCC; C++17 consumes
the public header. No Zig generation, compilation, linking fallback or runtime
delegation exists inside that archive.

The six source-classifier consumers remain on the SAME existing
zig_error_to_type function in runtime/zig/src/runtime/diagnostics/error_scope.zig.
No C copy/name table, error-name parsing, generated duplicate or per-provider
classification fork is introduced. The selected stack receives an already
classified uint32_t exactly as the retained stack does. The original file also
continues to provide compatibility constants, callback declarations and
ScopedError to retained callers. This is a PARTIAL owner migration, explicitly
not an accepted replacement for the whole file or a Zig-free native runtime.
Migrating the classifier and callers is a separately frozen later obligation.

The classifier maps OutOfMemory to 3; its 24 listed validation errors to 2;
every other Zig error to 4. The source manifest records the exact list. Surface
adds UnsupportedFeature/SurfaceUnavailable -> 2; fused compute adds
DispatchPreconditionFailed/Overflow -> 2 before invoking the shared classifier.
Their original diagnostic construction and all caller-local overrides remain.
Do not move caller diagnostics into the C stack.

## Exact retained behavior

- Filter 1 matches error type 2; filter 2 matches type 3; filter 3 matches type 4.
  Every other filter (including zero and UINT32_MAX) matches every type except
  NoError=1. Unknown numeric error types are preserved, not normalized.
- Deliver searches newest-to-oldest. The first matching scope consumes the
  error, even if it already has an error. Only its first error is stored: later
  matching errors neither replace it, reach an older scope nor reach the handler.
- Error type NoError matches no filter, including catch-all, and therefore is
  sent to the uncaptured handler when installed. It is not silently suppressed.
- Push at depth 64 does not mutate any slot, deliver an error or call a handler.
  C returns LIMIT; the native compatibility wrapper intentionally ignores this
  one expected status. The misleading retained comment does not change behavior.
- Pop consumes the newest scope before callback. Empty returns EMPTY and never
  invokes its callback. Null callback still consumes an existing scope. A clean
  scope produces (NoError, {NULL,0}); a captured empty message produces non-NULL
  data and length zero. Neither mode nor next_in_chain changes internal behavior.
- Both captured and uncaptured messages copy min(length,512) BYTES into zeroed
  512-byte local/record storage. Embedded NUL and split UTF-8 bytes are preserved;
  no terminator is appended at length 512. Input is borrowed through the copy;
  callback output is borrowed only through return. Empty uncaptured delivery also
  supplies non-NULL data with length zero. No original message is fabricated.
- Set-handler replaces callback and both userdata values in the same critical
  section. A null callback still stores its userdata values and disables future
  selection. Deliver snapshots the entire tuple under the mutex; replacement
  or clearing does not revoke an already selected callback or free its userdata.
- No matching scope and no handler silently drops the error, preserving source.
  DeviceDestroy clears the handler only; it does not pop or deinitialize scopes.

## Storage, lifecycle and failure discipline

The public opaque API takes caller-owned untyped allocated storage of the exact
queried size/alignment. A declared byte array, guessed capacity, union trick,
aligned byte buffer or unrelated struct cast is not an admitted C effective-type
workaround. Native embedding uses an actual C typed object imported through the
PRIVATE build-bound bridge, the established future-owner pattern. Public callers
need only the public header and libc allocation; the library never calls malloc,
calloc, realloc, free, a fallback allocator, VLA or alloca.

Requirements rejects NULL out before version; unsupported version leaves output
unchanged. Init checks out first, sets *out=NULL, checks version, non-NULL storage,
size then alignment before touching storage. Output/storage overlap is forbidden.
Init is exclusive into never-initialized or successfully deinitialized storage;
reinitializing a live mutex is a precondition violation, not detectable from
arbitrary uninitialized bytes. One mutex initialization succeeds or returns
SYSTEM_ERROR with the exact pthread result; failed mutex init owns no mutex and
storage is reusable. State is published LIVE only on success. No successful handle
or error callback is fabricated. A private typed init shares this same initializer.

Each ordinary operation requires a live non-NULL object, apart from diagnosable
NULL/invalid-state rejection. Wrong/freed pointers are not safely inspectable.
Deliver rejects NULL/nonzero length before locking. Valid pointers must cover the
bytes actually read, up to min(length,512), and must not alias mutable owner state.
No operation interprets WGPU_STRLEN; the native InjectError wrapper retains its
existing NULL-pointer -> empty conversion before entering C.

Lock failure returns SYSTEM_ERROR with the original pthread code, no mutation or
callback. An unlock failure is terminal inside C with operation and pthread code:
mutation may have occurred and lock ownership is uncertain, so no caller may
infer rollback/retry. Diagnostic termination is not cleanup success. Result
system_error is zero for non-platform statuses. On callback-free normal returns preserve entry errno. Immediately before foreign
dispatch restore entry errno; after dispatch preserve whatever errno the callback
left. Never overwrite callback-induced changes with an entry snapshot. No mutable global error slot is permitted.

Deinit requires external exclusion against any operation which could touch the
stack, including new entry. It does not wait or manufacture lifetime safety.
It destroys the sole mutex; success marks DEAD and permits free/reinit. Existing
scopes are discarded without callbacks. Destroy failure returns CLEANUP_REQUIRED
with the actual pthread error, retaining all storage and mutex ownership; only
exclusive deinit retry is allowed, not normal calls, free or reinit. The native
final-release path treats unexpected deinit failure as diagnostic termination and
never frees live synchronization. There is no process-wide cleanup registry.

## Lock, callbacks, reentry and lifetime

Copy pop's complete result, or uncaptured callback+userdata and bounded message,
into caller-frame locals before dispatch. Pop decrements depth while locked;
deliver captures while locked. Unlock before foreign code. The callback can
push/pop/deliver/set-handler on the same stack. Once dispatch begins, C and the
native wrapper MUST NOT dereference the stack again, including counters, result
publication or deferred cleanup. Return a saved result only.

No active-call counter or device-retain hook is added. This preserves synchronous
callback final-release safety where existing callers already establish it.
A callback can destroy/free its stack if no other access can occur, since its
invoking operation has finished all stack accesses. Conversely, a mutex does not
make concurrent final release safe: callers must establish object and userdata
lifetime for all concurrent accesses and previously snapshotted callbacks. Clearing
handler is not a synchronization fence for userdata release. Cross-thread callbacks
may overlap or occur in a different wall-clock order from mutex selection. No
cancellation, longjmp, thrown C++ exceptions, signal use or fork safety is admitted.

## ABI: three callback surfaces, not aliases

The authoritative WGPUStringView comes from pinned webgpu.h at
01addc4ba8a2915a061b7095a6768b512071ab96, SHA-256
7691d76ceb67056f89e97b9ff0b62163eacf19a8e0e09a8fa7dbb32069b7978c.
The C header declares Doe's internal four-argument callback with uint32_t type,
this exact by-value StringView, and two userdata pointers. Standard WebGPU pop
has a separate status argument; standard uncaptured has a device-pointer argument.
Do not cast among these signatures or call one through another.

The Zig adapter uses the existing legacy callback signature. Prefer direct typed
assignment if the imported types are exactly identical; otherwise use two narrow
extern declarations for pop/set_handler with the EXISTING authoritative Zig
WGPUStringView alias and legacy callback type. These are ABI declarations, not
casts or record replicas. Freeze and exercise both directions against the C
header: scalar widths; StringView size/alignment/data/length offsets; result
size/alignment/status/system_error offsets; function calling convention, argument
order, NULL/non-NULL pointers, distinct userdata sentinels, arbitrary uint32_t,
empty and full 512-byte values. The private imported stack/scoped-record size,
alignment and EVERY field offset must match GCC library probes before native init.
No public or private record is passed by pointer as an unrelated record. Any
mismatch is a build/admission failure, not permission for reinterpretation.

Keep these existing behaviors unchanged:
- Native invalid-device pop invokes INTERNAL=4 with {NULL,0}, then returns future
  5; native live empty pop invokes nothing and also returns future 5.
- Standard wgpuDevicePopErrorScope retains its existing real device lease,
  synchronously captures into ScopeReply, then uses the existing callback owner.
  Empty/invalid pop yields status3, NoError1, "no error scope to pop". Modes,
  future IDs, payload allocation failure and late resource release stay there.
- ScopeReply's existing message capacity derives from sizeof(retained ScopedError);
  it remains unchanged, but never uses that record's layout to access C storage.
- SetUncapturedErrorCallback remains Doe's legacy extension signature. Descriptor
  standard uncaptured callbacks are not silently installed or reinterpreted.

## Actual retained-device seam and edit ownership

Add support/doe_error_scope_owner.zig and doe_error_scope_c_adapter.zig. Only
DoeDevice.error_scopes selects the C adapter under experimental_c_error_scopes;
all existing producers keep .push/.pop/.deliver/.set_uncaptured_handler calls.
The retained error_scope.zig and tests/core/error_scope_test.zig remain unchanged.
The selected wrapper starts explicitly COLD with real imported storage, not a
lazy allocation, zero-as-initialized mutex or infallible fake initializer.

One production initialization site exists: create_device_for_adapter, immediately
AFTER create_device_object returns its final assigned DoeDevice and BEFORE
assigning loss/event.setDevice or publishing the device. This avoids overwriting
an initialized mutex in each backend's whole-struct assignment. No device scope
producer is reached inside create_device_object. On ordinary init failure, release
that unpublished device through existing ReleaseInternal while the wrapper remains
cold; this unwinds its existing backend/feature/cache/adapter ownership. Then
follow the existing Event failedCreation/release path and return the existing
DeviceAllocationFailed diagnostic, logging the exact new scope-init status/code
without inventing backend failure. No successful device is exposed.

Final ReleaseInternal calls checked scope deinit after existing resource cleanup
and immediately before alloc.destroy(d); a cold wrapper is a no-op. DeviceDestroy
retains its existing handler-clear call. Do not change refcounts, GPU drain,
unknown-completion policy, loss events, adapter ownership or callback payload code.
No scope object is moved/copied after initialization.

Exact production edit set requiring lead coordination:
1. New runtime/c/error-scopes/{include,src,integration} owner and build files.
2. New runtime/zig/src/native/support/doe_error_scope_{owner,c_adapter}.zig.
3. runtime/zig/src/native/support/doe_native_object_types.zig: field selection only.
4. runtime/zig/src/native/lifecycle/doe_instance_device_native.zig: explicit init,
   failure unwind and checked final deinit only.
5. runtime/zig/build.zig, source-layout.json, config/zig-test-inventory.json and
   generated selected test root; owner charters/MIGRATION for bounded coverage.
No producer/shared callback/classifier/ABI file edit is proposed. Shared build and
type files are NOT available until lead coordinates their current owners.

Add default-off explicit absolute -Dexperimental-c-error-scopes-archive path;
require native Linux x86-64, GCC-built PIC C11 archive without short enums,
strict warnings, libc/pthreads, provenance
and exact bridge checks. Error scopes need not depend on C futures/callbacks/
instance; exercise both error-scopes-only and the already accepted all-C CPU owner
combination. Keep all default selections and existing valid combinations. Reject
missing paths, foreign target, browser-WASM and retained generic suites with the
new selection rather than fallback or silently testing Zig. Wire the flag into
main/compute/full and the complete actual artifact closure, not only one DSO.
Use one configured-owner helper, not a second policy registry. Existing dirty
build changes must be reconciled by their owner before this seam is installed.

## CPU consumer evidence and exclusions

Canonical retained scope tests still prove their original Zig owner only. Selected
cases explicitly initialize the real DoeDevice at a stable address and exercise
actual native exports, requireAlive, command-recording validation/OOM and the
standard ScopeReply callback route. A test-only fixture may allocate the actual
DoeDevice/DoeAdapter topology and real instance to expose these CPU diagnostics;
it is NOT successful RequestDevice/GPU creation. No fake Vulkan/Metal runtime,
new production fixture export or substitute scope implementation is allowed.
Use actual production final Release/Destroy and original callback payload owner.
Externally compiled C/C++ consumers use the real DSO and typed production symbols.

Package and Bun bridges remain unqualified. Inherited Node routing prefers standard
wgpuDevicePopErrorScope when available but uses the native callback shape; this is existing ABI debt,
not to be silently fixed or counted as parity here. Successful device creation,
physical Vulkan/Metal/D3D12, configured surfaces, compiler success, full package,
proc-table/browser adoption, 32-bit/Windows/macOS and broad race freedom are separate
unrun gates. CPU tests cannot establish any of them. The selected migration adds
transitional Zig adapter/source obligations and does not improve the 50% figure by
hiding them or by moving storage off stack. EVALUATION.md owns proposed evidence;
independent acceptance must be separately authored and frozen.
