# Independent Instance 1 decisive cases, seal 1

Written 2026-10-23 before any Instance 1 candidate source was read or executed.
This is an executable-specification inventory, not a claim that test code or an
implementation already exists. The evaluator has read the exact frozen public
header/contract and author requirements, plus accepted predecessor consumers.
Expected observations below were independently chosen against those contracts.
Candidate implementation remains unread until this file and baseline receipt
are hashed. Later test code realizing these cases will have its own seal before
candidate reads/runs. Any new assertion learned from candidate review must be
labelled a supplement, never retrospectively called blind coverage.

Inputs: accepted predecessor 3582e86472db945cc441eedb3796ec7f1b13d179; contract
manifest SHA256 f224ddda8da7c7c0cea8b7dc2bbe4f5ad3771dce01fd6a65130d905037281919.
No compiler/M10 working-tree change is an input. No implementation edits, commit,
push, installs, downloads, hardware calls, private traces, or artifact deletion.

## Observation apparatus fixed before implementation

Object and wait allocators have distinct state addresses and vtable sentinels;
allocate/free log pointer, size, alignment, state and vtable to externally owned
records. Allocated, external-count and remove-label hooks log only, acquire no
references, and never reenter unpublished/dying objects. A queue-wrapper log
records real accepted calls and returns, not replacement queue behavior.
Test-only linker wrappers may fail the named pthread/allocation/status boundary.
Completed flags begin as 0x11, 0x22, 0x33, 0x44 to distinguish untouched, cleared
and completed slots. Assertions remain active in optimized builds.
Final-free observations are outside the object; never dereference freed memory
or hold a spare observer reference. Fatal subprocess logs are pipe-backed so
termination preserves whether forbidden label/free actions occurred.

C = standalone GCC owner + exactly one source-built accepted futures archive.
N = real retained Zig/native host fixture; P = public C/C++ loaded DSO consumer.
Four separately source-built controls use identical current frozen runtime
sources: Z (no C), F (future), FC (future+callback), FCI (future+callback+instance).
Historical and author-prebuilt consumption are separately labelled, never used
instead of an independent source build. C++ uses C++17, no exceptions or RTTI.

## Public owner cases (C)

C01 Validation precedence: create(NULL,NULL) and create(valid-config,NULL) return
INVALID_ARGUMENT; create(NULL,&out) overwrites nonnull sentinel out with NULL.
A config whose first readable uint32 is version 0 or 2 and whose later bytes are
in a protected page returns UNSUPPORTED_VERSION and out NULL without touching
later bytes. Test each of four mandatory allocator function members missing;
expect INVALID_ARGUMENT, out NULL, and zero allocation/hook calls. Null state
and vtable with stateless functions are legal and later free sees null tokens.

C02 Allocator snapshot: create with object state A/vtable VA and wait B/VB; then
replace every field of caller config with poison alternatives. Borrow twice and
copy the first lease through an overwritten local variable. Both queue pointers
and instance userdata match; copying/borrowing makes no allocation or reference
side effect. An empty issued-ID wait exercises B/VB and final release uses A/VA.
One object allocation/free only, same exact pointer/extent/alignment. Queue lies
inside that allocation at a correctly aligned nonoverlapping trailing address.
Observer sees exactly that extent/alignment before real queue-init wrapper entry;
remove-label occurs only after successful queue-deinit return, before object free.

C03 Lifetime count: create, retain twice, release twice: zero final frees. Final
release yields exactly one queue-deinit, label removal, and free, in that order.
Run 257 independent lifetimes and require net balance zero. NULL retain/release/
undo and process_events do nothing. from_handle(NULL) and aligned 64-byte live
storage with marker 0xDEADC0DE return NULL; authentic live instance returns same
pointer. No forged matching prefix or arbitrary/stale-pointer test is admitted.

C04 External guard versus undo: set external count=7; create; release preserves
allocation. Retain twice, release twice, release again still preserves one.
Clear guard, release finalizes once. Separately create with positive guard,
retain once, undo once: undo must not call external-count hook or finalize;
ordinary release at floor does call hook and preserves object. Clear guard and
release finalizes. Check hook key is original instance on every ordinary release.
All this is controlled C-hook evidence, not real texture success evidence.

C05 Object OOM: allocator fails exactly its first object request; expect
OUT_OF_MEMORY and out NULL, allocated/queue-init/label/free counts all zero.
Next fresh creation succeeds; OOM consumes no queue identity (compare first IDs
with a control fresh process when needed). Wait allocator is never used by create.

C06 Queue setup rollback: inject one failure at mutex_init, condattr_init,
condattr_setclock, cond_init, condattr_destroy, separately. Expected ordinary
queue setup error is QUEUE_INITIALIZATION/NULL; allocated fires once before init,
real accepted rollback completes before exactly one matching object free, no
label hook. A subsequent fault-disabled create/release succeeds in same process.
If the accepted queue reports CLEANUP_REQUIRED under a particular combined
rollback fault, C07 applies instead; do not relabel it ordinary rollback.

C07 Terminal setup/cleanup: cause cond-init failure plus mutex-destroy failure;
require diagnostic termination for cleanup-required initialization, no label/free
and no published handle. Separately queue a real registered unready completion
without an event lease, release sole instance: require pending/BUSY diagnostic
before label/free. Additional isolated wraps return BUSY, CLEANUP_REQUIRED and
SYSTEM_ERROR from actual final deinit boundary, and inject mutex/condition
destroy or unlock uncertainty: require operation+status diagnostics and no unsafe
free. Tests intentionally terminate, never pretend failure is recoverable.

C08 Count boundary private unit: under a private reduced maximum of 3, create,
retain twice, next retain terminates instead of wrapping; live private zero-count
fixture must reject retain/release; undo at 1 terminates without finalization.
Use test-only compile boundary or private C fixture, never a public mutation API,
old Zig layout overlay or stale native handle. Record exact instrumented source.

C09 Wait zero/invalid/overflow: count=0 with NULL instance+NULL infos returns
Success1 at timeout 0, 17 and UINT64_MAX, with zero allocation. Nonzero NULL
instance returns Error3 without changing sentinel flags. For live instance,
count=SIZE_MAX/sizeof(doe_future_wait_info)+1 and readable one-record pointer
returns Error3 before any record access, allocation, or flag mutation. A protected
record address is allowed only on paths contractually forbidden to dereference it.

C10 Wait OOM: create two valid issued IDs; initialize flags 0x11/0x22; fail wait
allocation, expect Error3, unchanged IDs/flags, no queue wait call. Restore allocator
and wait again successfully; original object remains live, no object allocator
used for temporary wait storage. On ordinary waits, free B/VB before final lease
release can trigger object free.

C11 Exact error copyback: on one instance issue A/B/C. Wait [A,0,B] with flags
[0x11,0x22,0x33], timeout0 -> Error3 and [0,0,0x33]. Repeat with second-instance
ID or legacy ID in slot1 -> same partial reset. [0,A] -> [0,0x22]. A plain issued
but unregistered valid ID -> Success1/completed1. Completed ID never re-registers.
No expectation that all error outputs are cleared or left unchanged.

C12 Deferred duplicates and timeout: register mode1 completion A not ready;
wait [A,A] timeout0 -> TimedOut2/[0,0], callback count0 and obligation retained.
A joined publisher marks ready, wait [A,A] UINT64_MAX -> Success1/[1,1], exactly
one callback and completion retirement/free. Second wait succeeds without another
callback. Repeat with mode2 and pump; modes0/3 deliver during readiness call.
No callback may occur under queue mutex (callback itself issues/waits/pumps).

C13 Ordering: register A,B,C in issuance order but publish C,A,B. Pump mode2
callbacks in A,B,C order. A callback issues D mode2: first outer pump delivers
A,B,C and leaves D; second pump delivers D. Separate variant has A explicitly
recurse into process_events after issuing D: inner fresh boundary may deliver
B,C,D before A callback exits, trace A-enter,B,C,D,A-exit. Do not demand universal
next-pump deferral. Wait order [C,A,B] still delivers lowest eligible IDs first.

C14 Final-caller destruction: payload owns one real instance lease; caller owns
one. Callback drops caller, finishes/deinits/frees itself, releases event lease.
For readiness, wait and pump entrypoints separately, instance remains allocated
through real accepted queue return then finalizes once before outer C return.
Wait temporary free precedes instance free. Store future ID outside payload.
Repeat on actual pthread publisher; no source/payload read after callback free.

C15 Self/other-thread: within blocked active callback, wait on its own ID on
same thread -> Success1/completed1. A second thread waiting same ID timeout0
while callback blocked -> TimedOut2/completed0. Release callback barrier, join,
then second wait -> Success1/completed1. External owner leases cover both threads.
No sleeps used as sole ordering proof; mutex/condition barriers establish phases.

C16 Contention: hold original owner while 8 pthreads each do 10,003 balanced
retain/release pairs, then join and release original. Exactly one finalizer and
zero live allocations. Separate event worker readiness uses real accepted queue
concurrency. ASan+UBSan instrument instance+future objects; TSan likewise. Runtime
startup failure is explicitly unexecuted, not successful race qualification.

C17 Layout boundary injection: wrap requirements to report alignment4096 and
size sufficient for real queue (accepted init still runs); observe 4096-aligned
queue and matching total object allocation/free. Requirements alignment3, zero,
or checked-addition/roundup-overflow must return LAYOUT_OVERFLOW/NULL before
allocation. No guessed private queue sizes. Unexpected requirements error must
be fail-closed; record exact mapping allowed by frozen contract, no fake success.

C18 Capability ABI: features count0/pointerNULL; has_feature false for 0,1,2,3,
0x7fffffff,0xffffffff; limits nextInChainNULL/max0. FreeMembers accepts a record
without dereferencing/freeing its borrowed feature pointer. C++ calls every
public owner function needed for lifecycle/borrow/wait/capability, validates
C-link names and struct-by-value returns rather than only compiling header.

## Actual public/native consumer cases (P/N)

P01 Fresh Z/F/FC/FCI DSOs: each process sets DOE_BACKEND=metal before dlopen and
first request. Resolve wgpuCreateInstance/AddRef/Release/WaitAny/ProcessEvents,
RequestAdapter/CreateSurface and capability/free functions directly and through
wgpuGetProcAddress for explicit byte length and SIZE_MAX sentinel. Compare local
function identity against actual manifest target, including native bridge aliases.
Resolve doeNative counterparts. No fallback/provider switching is permitted for
authentic selected instance. Retain original instance_feature_lookup.c unchanged.

P02 Four callback modes: create real instance with zero descriptor, request real
adapter with modes0/1/2/3. Exact callback status3, null adapter, bytes
"metal default device unavailable" (32 bytes), correct userdata1/2. 0/3 run before
request returns; 1 stays pending after pump and delivers in wait; 2 delivers in
pump or wait. In callback release last caller reference. Future identity must be
saved safely after synchronous payload free. Run both C and C++ consumers, each
DSO, direct and actual proc requests. No dummy adapter/device/backend object.

P03 Native no-op/error: NULL and readable nonmatching marker AddRef/Release do
nothing; nonzero WaitAny Error3; zero count Success1 at 0/17/UINT64_MAX. Use actual
live unconfigured surface as wrong-kind handle and preserve its surface lease.
Invalid ProcessEvents must drain a real registered ownerless work-done obligation
via original global registry; empty no-op alone does not satisfy this case.
Native historical CreateSurface(NULL/invalid-instance,valid-desc,NULL) may create
an ownerless real host surface on Linux, which is then released. Do not impose
standard foreign-provider routing policy on this separate native entrypoint.

P04 Actual surface retain: standard surface create with zero readable descriptor
and real instance yields actual unconfigured Linux DoeSurface. Release caller
instance, observe no finalization; borrowed raw instance remains usable solely
under surface lease. Request mode2 real unavailable adapter, pump to retire its
request, then release surface (public path, separate native-release variant).
Exactly one instance final free and no live surface/payload; no VkDevice, physical
VkSurface or presentation. Inject native host surface allocation failure before
retain: NULL, no extra instance lease; separate invalid nonnull readable device
bind failure after retain: NULL, surface freed and instance lease restored.

P05 Reentry/follower/free: request A/B mode2, callback A queues C mode2. First
pump trace A,B and C pending, second C. Recursive variant inner pump trace
A-enter,B,C,A-exit. Duplicate waiting entries deliver once. Callback self-waits
successfully. Null foreign callback still retires payload. All last-caller release
traces verified using test-only observation without spare references, then normal
unmodified DSO functional path rerun separately.

P06 Capabilities/ABI: direct/proc enumerate yields empty, all known/unknown feature
queries false; GetInstanceLimits Success1/NULL/max0 even after successful finite
wait; FreeMembers no-op. WGSL-language exports remain same as Z baseline. Verify
all imported WGPU/allocator/lease/feature/limit field offsets, sizeof/alignment,
scalar widths, enums, function signatures and by-value return ABI using GCC and
Zig probes, not sizeof alone or unrelated-pointer overlays.

N01 Short-lived view: actual real C instance, actual selected Completion inside
heap payload, acquire event lease, issue/register against copied QueueView inside
a short stack scope, overwrite that view, publish from joined worker. Callback
finishes/frees payload and caller+event references; only full-call lease survives
until readiness returns. No stored view address, extra queue or implicit copy lease.

N02 Late dispatch and preparation: actual Completion register with dispatch NULL;
install dispatch only after registration, publish and require one successful
callback/finish/free. Also finish never-initialized Completion, ownerless prepared
completion, unregistered finish/deinit. Compile actual compute/render registration
and cleanup callers, trace their same identity and instance_pending route. This
is host completion evidence, never a successful pipeline qualification.

N03 Actual loss Event: Event.create with real C instance, null device, original
References and mode2; lose(.destroyed,"instance-evaluator-loss"), then duplicate
lose(.failedCreation,"ignored"). Release event creation reference before pump;
callback sees null device and first reason/message exactly once, releases caller
instance; actual event retires/frees then pending lease releases. Separate failing
allocator at Event allocation -> no instance retain and no ID issuance; compare
next identity sequence against before/after allocation failure, not global fixed ID.

N04 Task pool: reuse preserved accepted callback obligations with actual submit,
worker publication, submission-failure immediate fallback and shutdown join. Stack
message/context bytes are overwritten after enqueue; deferred callback still sees
copied original bytes. No foreign C allocation policy or fake worker substitution.

N05 External guard route and original tests: source/compile audit both actual
texture speculative rollback sites call instance_undo_retain, successful registration
and final Release keep old ordering/key. Original future_ids/callback_delivery
source hashes equal accepted predecessor. Execute six original future tests, two
original callback tests, all eleven accepted selected future obligations in F/FC,
five selected callback obligations in FC, and named FCI adaptations preserving
all assertions. Default loss/lease/pipeline cleanup, legacy work-done/reentry,
allocator/label/trace tests run where selected; unmatched filters never count.

## Structural acceptance and accounting

B01 Source-built ownership: archive nm/object inventory shows instance archive
contains only instance owner, exact one linked accepted queue and callback object
set, no Zig C compiler or delegation. Audit sole C atomic count, one untyped
allocation with disjoint typed control+queue, no instance Zig shell/refcount/queue,
full-call leases, snapshot-before-free, no callback under queue lock, release
memory orders and native instance magic from C prefix. Explicit imports acyclic.

B02 Four-route closure: inspect generated options and symbols per DSO: Z has no
C owner symbols/dependency; F no callback/instance; FC no instance; FCI exact three
archives and instance flag true. Independently rebuild once incrementally and
record duration/hash. Inventory main/compute/full and every configured artifact
root in build.zig, distinguish built from audited-only. Reject instance-only,
instance+futures/no-callback, relative/missing archives, unsupported target,
browser-wasm and incompatible generic/old focused tests before execution.

B03 Bind evidence: verify source hashes before/after compilation; original 123
baseline binding entries plus expanded build closure, exact candidate version,
header/archive/DSO/consumer hashes, target/compiler versions/argv/dependencies,
first failure and repaired run. Candidate changes after seal require fresh identity
and rerun of affected cases. Source tests and author-prebuilt tests never merge.
Record clean/incremental costs, allocation counts, artifact sizes, source deltas,
discovery/test/build/blocked time; no unmeasured quality/performance improvement.
Run changed-owner source-layout/import/format/charter/index/inventory gates.

No pass may be inferred for successful adapter/device, real texture, configured
surface/presentation, GPU completion/timeout/loss, Vulkan/Metal/D3D12 hardware,
N-API/Bun/Dawn proc-table/package/browser adoption or complete migration.
