# C future identity and completion queue, ABI 1

Status: frozen contract; implementation is not yet accepted. This is a separately
buildable CPU owner, not a production provider switch or whole-runtime migration.

## Source and consumer boundary

Baseline commit: `665475907494d2095a760596218b4c2d363cb436`.
The actual owner is
`runtime/zig/src/native/support/doe_future_ids.zig`, SHA-256
`be44acc738915f39412cfc4108f8204e31acbe59551fbc92c613d67d9ad59e92`.
Its six inline tests are the retained source-bound baseline, not new acceptance
claims. Preserve the root MIT license and Doe contributors attribution.

`doe_callback_delivery.zig` allocates request nodes, owns payloads and instance
leases, registers, then publishes exactly once. Its dispatch calls foreign code,
releases payload/resource obligations, finishes, frees the node and releases the
instance. `doe_instance_device_native.zig` retains the instance around WaitAny and
ProcessEvents. Those ownership layers stay outside this increment. This library
has no allocator, instance/device reference counter, GPU operation or WebGPU ABI
export. The new header does not claim binary interchangeability with WGPU types.

## Storage and platform

C11 and POSIX pthreads on Linux, monotonic condition-variable deadlines, native
C calling convention; public header is also consumable by C++17 without
exceptions or RTTI. There is no Zig generation, compilation, linking or execution
in any C build/test. Retained-Zig characterization is a separately invoked path.

Queue and completion are opaque. Versioned requirements report exact size and
alignment for the current library/build; those requirements may change across
builds with the same API version and must be queried again. The caller supplies suitably aligned allocation storage with no
declared type (for example malloc, or aligned_alloc when necessary), keeps it
stable, and eventually frees it. Arbitrary declared byte arrays are not admitted
as typed storage. Init validates version, size, pointer and alignment before
publishing a handle. Ordinary failed init leaves *out NULL and storage reusable.
Exceptional CLEANUP_REQUIRED returns a cleanup-only queue handle in *out; keep
its storage and invoke only exclusive queue_deinit retries until OK. No free or
reinit is permitted before successful cleanup. Init of a live object, overlapping
objects, aliasing out with storage, and copying/moving live objects are forbidden.
No library allocation/free or implicit synchronization initialization exists.
Partial mutex/condition-attribute/condition initialization must roll back. Track
which members were successfully initialized, including the condition attribute,
and clear ownership only after successful destruction. Stop cleanup at its first
error and return CLEANUP_REQUIRED without pretending resources were released.
Retry destroys only still-owned live members. Completion init owns no
synchronization object and cannot need partial-cleanup recovery.

Every operation requires a successfully initialized live object. Invalid/freed
pointers cannot be validated by this API. Non-null spans and out pointers must
name correctly aligned writable objects of their declared type and full length;
input/output overlap is not allowed. Wait-info memory remains caller-owned and
exclusive throughout the wait, including callbacks. Init, completion deinit and
completion registration require exclusive access to that completion. Queue
functions otherwise synchronize shared state; caller must not race finish/free
against publisher entry or manual delivery.

## Identity and registration

Instance IDs occupy 1..0x7fffffff; sequence values occupy 1..0xfffffffe.
An instance namespace is acquired lazily on its first ID issue. IDs are
(instance << 32) | sequence. A separate high-bit legacy namespace begins at
0x8000000000000000, through 0xfffffffffffffffe. IDs never wrap or recycle,
including queue deinit/reinit. Global uniqueness means one linked library
instance only; independently loaded copies/DSOs and fork are excluded.

Registration borrows a live initialized completion and requires an ID already
issued by the same queue. It rejects an active duplicate ID or active node and
foreign/future/zero IDs. Every issued ID may be registered at most once, ever;
this remains the private production owner's precondition, not a tombstone-backed
claim. Retired IDs and issued-but-unregistered IDs are considered complete.
Re-registering a retired ID violates the contract even if not detected. Nodes may
be reused after finish/deinit/init with a newly issued ID and external exclusion.

## Readiness, delivery and retirement

Callback modes have pinned values 0 (flat-native compatibility), 1
(WaitAnyOnly), 2 (AllowProcessEvents), 3 (AllowSpontaneous). Other modes reject at
init. Dispatch and userdata are fixed until deinit. Null dispatch is admitted
only for a manual-retirement obligation that never calls mark_ready. Publication
requires non-null dispatch, happens exactly once while storage is alive, and does
not grant stale-pointer or repeated-publication safety.

Registered modes 0/3 mark ready and mark the publishing thread as delivering,
then dispatch synchronously outside the mutex. Modes 1/2 publish and wake waits.
Unregistered mark_ready dispatches immediately, as the source's ownerless path
does; it does not invent a registered obligation. A manual begin_delivery marks
the current thread without calling dispatch and is allowed only before delivery
has started. Its caller owns the callback and lease extent.

ProcessEvents captures next_sequence once at entry. On EACH selection it chooses
the lowest currently ready, non-delivering, non-WaitAnyOnly ID below the captured
boundary. A pre-boundary ID made ready reentrantly may run during the same pump;
an ID issued during the pump waits for a later pump. Recursive ProcessEvents is
a new pump with its own issuance boundary and can deliver newly issued followers.
Concurrent pumps do not
promise a total global callback order. WaitAny similarly chooses the lowest ready
requested ID regardless of mode; duplicate wait entries never duplicate dispatch.

All dispatch callbacks run outside the mutex and may reenter any admitted queue
operation. A wait on its own delivering thread considers that future complete;
another thread considers it pending until finish. The callback must settle its
node with finish after foreign callback/resource cleanup; it may then deinit and
free the node before returning. The library must not read or write the node after
dispatch, even to update counters. The saved queue remains alive until every outer
call returns. Callback return without finish leaves the obligation registered and
delivering; it does not fabricate retirement or authorize free.

finish unlinks the node, clears its owner/link and wakes waiters. An ownerless live
node makes finish a no-op. Outside its delivering callback, finish requires caller
exclusion of concurrent selection/publication before freeing the node. A mutex
cannot protect already freed caller storage. Self-wait's complete bit permits
reentry, not owner/payload destruction before their actual lifetime boundary.

## Wait and errors

Zero count succeeds. Nonzero wait first clears each visited completed field and
validates its ID in input order, returning UNKNOWN_ID on the first unknown; fields
after that entry remain unchanged, matching the source. Each scan writes
completed=1 for retired/unregistered IDs or own-thread delivery, otherwise 0.
Ready requested callbacks are dispatched and the scan repeats BEFORE returning
success for another already completed ID. Success means any requested ID is
complete; count-zero is the sole empty-set success case.

Finite timeout captures CLOCK_MONOTONIC once before locking and uses one checked
absolute deadline. Callback time, reentry and wakeups do not restart the budget.
Timeout cannot preempt callbacks or bound their wall time. Zero timeout still dispatches ready requested callbacks before polling returns.
Readiness/completion is scanned before elapsed time is checked, matching the
source: a completed result can win after callback time consumes the budget.
UINT64_MAX is indefinite; arithmetic that cannot fit a native timespec returns
TIME_OVERFLOW. A condition ETIMEDOUT remains TIMED_OUT even when publication races;
a later call observes eventual settlement. Timeout never cancels or retires work.
Clock/condition failures return SYSTEM_ERROR. No timed GPU-completion claim exists.

C-boundary adaptations, not exact error parity: exhaustion returns EXHAUSTED
instead of Zig panic; invalid versions/storage/modes, active duplicate nodes/IDs,
null dispatch readiness, invalid manual state, synchronization setup failures
and cleanup-only handles/statuses have explicit status values rather than assertion/unreachable/unsupported use.
Global exhaustion saturates rather than wrapping. UnsupportedFutureWait in Zig
is unreachable for valid issued inputs in this owner, so has no public C status.
Normal mutex errors after successful initialization denote violated lifetime or
platform invariants, not a recoverable cancellation protocol.

## Deinitialization and concurrency boundary

Callers must establish external quiescence/no new entries before destruction:
no thread may newly enter, be waiting to enter, or retain an intention to access
an object that might be destroyed. Queue deinit does not wait for quiescence;
ordinary mutex acquisition can wait. It checks both
pending nodes and active library operations, including waits and the entire
outside-lock dispatch interval. A reentrant library-dispatched callback's attempt returns BUSY even
if it already finished/freed its last node. An outstanding wait/pump/publication
keeps the queue active; head==NULL is never sufficient. BUSY does not change the
live object. Successful deinit destroys synchronization objects and permits free
or reinit. On a destroy failure, deinit returns CLEANUP_REQUIRED and leaves a
cleanup-only handle: retain storage, disallow all ordinary operations and retry
only exclusive deinit. Track each live synchronization member and stop at the
first cleanup error. Do not race deinit against competing deinit or unaccounted
entry.
Completion deinit requires an unregistered node and exclusive access; callback
may do so after finish because the dispatcher never touches the node again.
The active-call BUSY guarantee covers library-dispatched callbacks only. After
begin_delivery returns, an external/manual callback has no enclosing library
dispatch call; its owner and payload lifetime remains entirely caller-managed,
especially after finish removes the last pending node.

Thread cancellation, signal-handler use, longjmp/C++ exceptions through callbacks,
fork, cross-DSO global identity, non-Linux ports and forced instance abandonment
are excluded. Callbacks must return normally. There is no shutdown-cancellation,
background worker, implicit finalizer, persistent wait handle or allocation API.

## Acceptance before migration claims

1. Retain exact source identity and all six unchanged source tests, separate from
   the C executable. Record first failures and fixes without rewriting evidence.
2. C tests cover valid IDs, cross-owner rejection, non-reuse, active duplicates,
   issued/retired waits, all modes, lowest eligible selection, issuance boundaries,
   late readiness, duplicate wait IDs, self versus other-thread delivery, and
   callback-driven finish/deinit/free/reentry.
3. Pthread condition handshakes, joins and startup checks cover infinite/finite
   waits, concurrent pumps, deadline expiry and destruction BUSY while a callback
   has already retired/freed its node. No sleeps as correctness proof.
4. Test exhaustion and clock arithmetic boundaries plus fault-injected mutex,
   condition-attribute and condition setup/destruction. Ordinary setup failures
   with successful rollback leave reusable storage. Cleanup failures preserve
   a cleanup-only handle; exact live-member retries lead to eventual reuse only
   after successful deinit. Never silently discard a destroy failure.
5. Separate C and restricted-C++ native consumers build against the public header
   and static library, plus strict warning, optimized and sanitizer checks where
   the installed runtime supports them. Independent evaluation is required.

These CPU checks do not qualify payload allocation/leases, physical GPU completion,
all native callback/device lifecycle consumers, production integration, performance
or full Doe-to-C migration. Their owners retain all original gates.

## Build-only identity exhaustion tests

Production endpoints are unchanged. A separate test build may define
DOE_FUTURES_TESTING and override DOE_FUTURES_TEST_INSTANCE_LIMIT (last issuable
instance, default 0x7fffffff), DOE_FUTURES_TEST_SEQUENCE_LIMIT (last issuable
sequence, default 0xfffffffe), and DOE_FUTURES_TEST_LEGACY_LIMIT (last issuable
legacy ID, default UINT64_MAX-1). Defaults apply unless explicitly overridden.
All endpoints stay inside their original nonzero or high-bit namespace. Overrides
without DOE_FUTURES_TESTING reject at compile time. The same production transition
code runs with these limits; no public mutation API or alternate state machine
is admitted. These tests establish saturation/rejection, not namespace size.
