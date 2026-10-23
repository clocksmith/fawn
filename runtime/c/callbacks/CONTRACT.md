# C callback payload ownership, ABI 1

Status: frozen after independent contract review. No implementation or acceptance
is claimed. The [seal](CONTRACT.sha256) binds this contract, the
[public header](include/doe_callbacks.h), the [retained seam](integration/CONTRACT.md)
and baseline manifests. Independent evaluator obligations must also freeze before
implementation begins. The accepted futures contract and historical evidence are
not amended by this owner.

## Responsibility and retained policy

Baseline: `6b9fea1d581f4d78a7a86a76beddd5a43ff1a965`.
[Provenance](PROVENANCE.md) binds the actual source owner and its consumers.
C owns callback payload allocation, typed context transfer into that allocation,
event-held instance lease, registration/publication, callback extent, completion
retirement, payload free and the independent readiness-call instance lease.
The existing [C futures owner](../futures/CONTRACT.md) alone owns identity,
registration state, selection, waits and readiness synchronization. No second
pending list, ID counter, scheduler, event framework or generic allocator is added.

Typed per-API Reply definitions/deliverers, their retained resources and ordinary
native object/refcount policy stay in Zig. Device-loss Events, successful async
pipeline requests, task pool, backend/device objects and GPU completion remain
separate. This API does not add WebGPU exports or claim WGPU/C record aliasing.
C11/POSIX Linux x86-64 and C++17 header consumption are the initial host envelope.
Only installed GCC builds C; approved Zig builds the explicitly retained adapter.
The C archive has no Zig generation, build, link or runtime dependency.

## Request and callback boundary

The request, optional owner record, source context, publisher userdata and output
are borrowed through the synchronous publish call. Their storage must remain
valid through return, including reentrant callbacks. Request/owner bindings stay
immutable; the source value is mutable only by its ownerless deliverer. Output
may not overlap request/owner/context storage. None may overlap storage that a
callback may destroy or be concurrently mutated by another thread. Invalid/freed pointers
are precondition violations, not safely diagnosable inputs. Native calling
convention applies to every hook; callbacks must return normally. No exceptions,
longjmp, cancellation, signal-handler use or fork safety is admitted.

Allocator, lease and deliver hooks can execute on different producer, wait/pump
or worker threads. Their shared state and userdata must support that admitted
concurrency. C adds no serialization around those hooks and does not promise
deallocation on the allocation thread. The accepted queue only synchronizes its
own readiness and delivery state; it does not make external hook state safe.

The native generic adapter creates `var local = context` and passes that mutable
local with `@sizeOf(Context)`, `@alignOf(Context)`, a typed assignment thunk and
typed deliver thunk. It does not allocate another event or own admission policy.
Non-extern Zig structs never become C record definitions. C sees only declared
callback userdata, its exact extent/alignment and typed hooks. The copy is an
infallible value initialization at the final destination, not memcpy-based deep
cloning. Inline arrays copy; pointers/slices retain their existing pointee owners.
No hook may retain the source pointer. Copy cannot allocate, fail, acquire leases,
invoke foreign callbacks or reenter publication. Deliver may reenter, includes
all foreign callback/resource cleanup, and must not retain the context address.

`owner == NULL` is the only ownerless form. An owned record requires a live
initialized accepted-C queue, retain and release hooks, and userdata whose lease
keeps that exact queue/owner alive. The record is copied by value, not retained by
address. The final release may destroy the queue/owner. Hooks are infallible;
the caller keeps the owner alive through admission/transfer. Every owned event
requires a lease binding, including standalone C consumers; no implicit unleased
fixture variant is added.

The allocator record stores copied callback userdata identities (`state` and
`vtable`) plus allocate/free hooks. For Zig these are the actual
`std.mem.Allocator.ptr` and `.vtable`, not a pointer to a stack Allocator value.
The thunk reconstructs the same allocator. Underlying state/vtable/hooks must
outlive every outstanding payload, including deallocation. Allocation returns
NULL on failure or an exclusive untyped block satisfying the requested extent
and power-of-two alignment. No declared byte-array effective-type workaround.
Free receives the original pointer, exact requested extent and alignment.
Allocation/free hooks must not transfer the source context or enter queue policy.
An allocator returning an invalid/misaligned pointer violates its contract and
terminates diagnostically; it never creates a recoverable transferred result.

## Validation, layout and pre-transfer failure

Validation order is fixed and directly testable:

1. NULL request or output: INVALID_ARGUMENT, without dereferencing request.
2. Unsupported abi_version: UNSUPPORTED_VERSION.
3. Mode outside 0/1/2/3: UNSUPPORTED_MODE.
4. Absent owner with mode1/2: INSTANCE_UNAVAILABLE, even if context/hooks are bad.
5. Missing source/deliver or zero/non-power-of-two context alignment:
   INVALID_ARGUMENT. A non-NULL source is required even for a zero-sized Context.
6. Ownerless0/3: take the immediate path below. Do not inspect allocator, copy,
   publisher or context_size, or calculate an owned layout.
7. Owned: missing owner queue/retain/release, allocator allocate/deallocate, copy
   or publisher: INVALID_ARGUMENT. Then query/check layout, returning
   LAYOUT_OVERFLOW on arithmetic overflow; allocation NULL is OUT_OF_MEMORY.

No returned error invokes copy, retains a reference, issues an ID, calls deliver
or publisher, or changes the output. No source bytes are read on rejected paths;
source storage must satisfy the typed hook's requirements if that hook executes.
Non-NULL invalid request/owner/hook pointers remain caller precondition violations.
Bad mode and no-instance mapping preserve the retained source's
UnsupportedCallbackMode and CallbackInstanceUnavailable errors.

One allocation contains a private typed C header, correctly aligned opaque
accepted-C completion storage, and final context storage. Query the accepted
completion requirements for this exact library build, not a guessed constant or
private copied type. Compute each offset with checked alignment round-up/addition:
header end -> completion alignment/extent -> context alignment/extent. Reserve
at least one context storage byte for a zero-sized Context. Allocation alignment
is the maximum header/completion/context alignment; do not assume max_align_t or
malloc is enough. Allocation size need not be a multiple of that alignment, so
an allocator using aligned_alloc must account for its own extra requirement.
The owner still frees using the exact original requested size/alignment.
No VLA, alloca, runtime-sized stack copy, separate payload allocation or hidden
fallback allocator is permitted.

Overflow yields LAYOUT_OVERFLOW, allocation NULL yields OUT_OF_MEMORY. Both are
before identity issuance, context copy, event reference, registration and
publisher. The native adapter maps both to existing `error.OutOfMemory` and
leaves the context/resources with its caller. INVALID_ARGUMENT and unexpected
version failure from the typed native adapter indicate adapter invariants and
terminate rather than invent a new recoverable native error. Unsupported mode
and no-instance errors map exactly as above.

This is transfer rollback only. BufferMapAsync may already have changed mapping
state, PopErrorScope already popped/captured a scope, and QueueWorkDone may have
flushed the queue. C does not undo those effects. Caller catch blocks retain
exact existing cleanup and fail-fast behavior; do not claim operation atomicity.

## Ownerless immediate path

With owner absent and mode 0/3, invoke deliver on the adapter's already separate
mutable local source value. Do not allocate, call copy, register, take a lease or
invoke the supplied publisher. After deliver/resource cleanup returns, obtain
one legacy ID from the SAME accepted futures library and return it. Reentrant
ownerless callbacks therefore obtain their IDs before the enclosing request.
The source-local value remains valid through delivery; C performs no dynamic
stack copy and retains no pointer. A null foreign API callback does not skip its
typed deliverer/resource cleanup. Legacy exhaustion is terminal after delivery,
never a returned error that authorizes the caller to clean resources again.

## Owned admission and publication

After validation/layout, allocate first. Only then issue an instance ID. Initialize
the private header and completion, perform the typed value copy, acquire one
event-held owner reference, register on that queue, then invoke publisher exactly
once. Source resource obligations transfer with successful admission and are
retired only by the typed deliverer. The ID is a local snapshot independent of
event storage. Publisher can synchronously dispatch/free the event before it
returns, or hand the token to one worker. After invoking publisher, publish writes
only its saved ID to the still-valid output and returns OK; it must not read the
event, context, owner or completion again. A worker may likewise finish before
publish returns.

Publisher is a void hook because accepted transport guarantees eventual exactly
one readiness publication. A failed task-pool submission is failure-before-job-
transfer and must immediately mark ready on the publishing thread. It is not a
returned publication failure, cancellation or abandoned event. Publisher userdata
is borrowed only during its invocation, not stored in the event. A worker owns
only the single live token and its transport obligations. Every user of the token
must stop using it at mark-ready entry; even the return cannot prove it is live.
Repeated/stale publication is outside the contract.

`doe_callback_ready` supplies an immediate mark-ready publisher. It adds no alternate
state machine. The completion mode remains accepted-C policy: owned 0/3 delivers
inside readiness; mode 1 requires a matching WaitAny; mode 2 admits wait or pump.
Timeout never destroys or retires the event. Selection, issuance boundaries,
reentry, duplicate waits and self-versus-other-thread semantics remain the exact
accepted queue behavior.

## Dispatch, retirement and independent full-call lease

C dispatch runs outside the accepted queue mutex. Snapshot the allocator binding,
allocation pointer/extent/alignment, completion pointer, context/deliver pointer
and owner lease before foreign code. Execute, in this order:

1. Typed deliverer, including foreign callback and per-API resource release.
2. Accepted-C finish. The obligation remains registered throughout step 1.
3. Accepted-C completion deinit.
4. Free the one C event allocation with the saved exact allocator binding.
5. Release the saved event-held owner reference.

No event memory is accessed after free. The accepted queue may still need its
queue after dispatch returns. Therefore mark_ready snapshots the owner lease and
completion pointer, takes a DISTINCT full-call reference, calls accepted-C
mark_ready, and releases the full-call reference only after that call returns.
It never rereads event storage after entering dispatch. On normal return this
can be the last reference, triggering real final instance destruction only after
active C accounting finished. An observer reference must not hide this boundary
in acceptance. The event reference alone is insufficient.

WaitAny/ProcessEvents remain in the already accepted native future adapter and
public native entrypoints, retaining their existing full-call references. Those
leases must not be weakened or removed for pipeline/device-loss/other consumers.
Standalone C callers using direct queue wait/pump similarly hold an independent
owner reference through the complete call and obey accepted external quiescence.
This owner provides no new queue pump wrapper, abandonment, manual retirement,
inspection, cancellation or destructor API.

## Terminal failures and external exclusion

After allocation, any unexpected completion-requirements/init/ID/register/
readiness/finish/deinit status is terminal, with the operation and returned C
status in an inspectable `C callbacks ... failed closed: status ...` diagnostic,
followed by abort. Requirements failures can occur before allocation but are still
build/library invariants, not transfer-recovery statuses. ID/legacy EXHAUSTED is
terminal. There is no configurable fatal callback or ordinary SYSTEM_ERROR return.
A readiness/finish SYSTEM_ERROR may follow mutation: never return control to a
caller catch block to release unknown ownership, republish or claim rollback.
No successful ID/status may be fabricated. Fatal paths make no cleanup-success
promise because the process terminates; test them in isolated subprocesses.

All accepted queue preconditions still apply. Exactly-once publication and a live
token are caller obligations; no mutex can validate stale/freed storage. Instance
final destruction remains its native owner, including external-texture guards
and existing fatal cleanup mapping. This layer does not create a new recovery
protocol for platform synchronization failure.

## Acceptance before implementation admission

Independent review and lead freeze must agree the following inventory BEFORE
implementation. Implementation acceptance requires separately authored evaluation;
these planned cases are not results and cannot weaken accepted queue tests.

1. Plain C and restricted C++ consumers use only the public headers and the same
   GCC-built callback archive plus one accepted futures archive. Audit symbols,
   archive members, source identities and no hidden Zig/duplicate queue code.
2. All returned validation/layout/OOM failures preserve output, ID stream,
   context, references, copy/publisher/deliver counts and caller cleanup. Verify
   exact allocation/free pairing, over-alignment, size overflow and zero context.
3. Ownerless mode0/3 is allocation/copy/publisher-free with mutable local delivery,
   resource cleanup for null foreign callbacks, and reentrant callback-before-
   legacy-ID ordering. Modes1/2 and unsupported modes preserve error precedence.
4. Typed context copy survives source-local overwrite, including inline diagnostic
   arrays; slices retain their declared external owner rather than fake cloning.
   Preserve actual ScopeReply and CompilationReply behavior at retained consumers.
5. Owned immediate/deferred delivery checks exact cleanup order, callback-time
   registered obligation, self/other-thread wait, issuance ordering, reentry,
   last-caller release, payload free before readiness returns and actual final
   owner destruction without observer leases. Retained accepted tests remain.
6. Existing task-pool transport exercises queued lifetime, submit failure before
   transfer/fallback and draining shutdown; distinguish adapted CPU transport
   from physical Metal QueueWorkDone. No new scheduler or device substitute.
7. Real exported headless RequestAdapter with a real native instance and fresh-
   process DOE_BACKEND=metal exercises actual unavailable-path payload allocation:
   status3, NULL adapter, `metal default device unavailable`. Modes0/3, modes1/2,
   callback reentry and last-owner release are checked in default and selected
   artifacts. No fake adapter/device or Vulkan probe substitutes for this path.
8. Additional real null-handle APIs are explicitly ownerless status/cleanup
   evidence, not owned-payload/backend success. Preserve selected/default test
   distinctions and the source-bound API statuses listed in the seam contract.
9. Fatal ID/init/register/readiness/retirement failures have exact diagnostics;
   pre-mutation and uncertain post-mutation failures are not conflated. Applicable
   C ASan/UBSan/TSan observations state which retained Zig code is not instrumented.
10. Default build has no callback/future C header/archive/symbol dependency;
    futures-only selection remains supported; callback-only selection rejects;
    selected headless consumer is reproducible using existing tools only.

Physical successful adapter/device/resource paths, Metal GPU worker completion,
Vulkan/D3D12 submission/readback, physical map/unmap races, real loss, external
textures, instance abandonment, package/browser adoption, release qualification,
performance and complete runtime migration remain open. Production/default
replacements remain zero. Transitional adapters are counted, not omitted.
