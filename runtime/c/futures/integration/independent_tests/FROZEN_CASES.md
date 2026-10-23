# Independent native C-futures evaluator cases

Frozen 2026-10-23 04:41 UTC before inspecting any candidate implementation.
Inputs: integration contract at 17a1fc31011d1ec6dee15126f362f2b67a2870e8,
C public API and frozen accepted owner b9b07dfa32313f074e6f34a1ccf516fc27075ada,
original callback_delivery and instance lifecycle via git show 17a1fc3,
original doe_future_ids baseline and engineering lead's preimplementation cases.
The lead's list is requirements, not independent executed evidence.

## Decisive cases and oracles

E01 Build/source identity: verify frozen source hashes, original Zig owner unchanged,
five producer/type imports select one facade, selected owner is real GCC PIC archive
with no Zig C compilation, main and both drop-in tiers have coherent selection.
E02 Default-off artifact built without archive/includes has no defined/undefined
C future or bridge symbols; explicit nonexistent selected archive rejects rather
than defaults. Exact headless native shared artifact and C/C++ consumers run source
built, not only developer binaries. All compiler/build flags are retained.
E03 Native CreateInstance yields real heap instance; AddRef/Release and final
queue destruction execute actual retained lifecycle. Counts/queue destruction may
be observed but not replaced. Report allocator-level free proof separately.
E04 Actual callback_delivery.ready/publish for modes 0,1,2,3 plus unsupported mode
and ownerless permitted/forbidden cases. Failing payload allocation returns OOM,
keeps context with caller, and preserves instance refs and pending obligations.
E05 Immediate and spontaneous callback releases last caller instance ref, then
actual dispatcher finishes/frees payload and releases event ref before returning.
No observer reference. Queue deinit must occur only after C active accounting ends.
E06 Real worker receives published completion, caller releases its ref before
worker readiness. Worker callback frees payload and final queue cleanup follows
full C call return. Thread creation/join is checked and ordering uses handshakes.
E07 Native WaitAny and ProcessEvents with deferred callbacks releasing last caller
ref, duplicate requested IDs, reentrant self-wait and nested ProcessEvents. No
callback duplication. No deadlock. Public outer lease and adapter lease are real.
E08 Another thread polling/waiting while callback is active observes pending until
finish. Zero/finite timeout does not retire the event or release its instance.
Use explicit handshakes rather than sleep timing as correctness evidence.
E09 Two real instances, unknown/foreign/legacy IDs, prefix flag reset on invalid
ID; duplicate valid records; issued-retired ID versus pending ID. Complete record
set is one wait and retains lowest eligible ordering independent of request order.
E10 Pump issuance boundary: first callback makes an already-issued pending node
ready and issues a new follower. Outer pump delivers original late-ready node,
not new follower; nested pump may deliver follower. Serial callbacks order by ID.
E11 Register with dispatch unset then assign dispatch before ready, matching real
pipeline pattern. Callback finishes/deinitializes/frees its wrapper before C
returns. Never-initialized finish is no-op, initialized ownerless finish cleans
resources, outstanding registered obligations block destruction fail-closed.
E12 Setup acquisition failures unwind and CreateInstance returns NULL, while
cleanup failures terminate with diagnostic in isolated subprocesses. Inject each
pthread acquisition/destruction seam without replacing native refcount/free.
E13 Wait-copy allocator failure preserves original flags; multiplication overflow
checks source-bound or directly exercised if exposed; count0 success with no
instance; owned copy, all resulting prefix flags copied back even error, no alias
cast or per-chunk waits. Cleanup exact on all ordinary paths.
E14 Typed C/Zig queue/completion size, alignment and offsets checked inline and
actual archive/header identity bound. No guessed bytes, fake head, hidden lazy
queue or post-initialization moves. Completion trampoline snapshots callback.
E15 Independent tests exercise actual imports and native exports with no alternate
queue models. Default tests run separately from selected adapted fixtures. Tests
and needed source snapshot are portable with MIT attribution and source hashes;
no archived dependency or sys.path manipulation.
E16 Applicable ASan/UBSan/TSan checks record which C and which Zig code is actually
instrumented, first failures, no unsupported general race/leak or GPU claims.

## Execution boundaries

Candidate is still mutable; do not inspect/run until author reports stable seal.
Only evidence directory is writable by evaluator. Snapshot only necessary source
inputs and no compiler copies. Current disk free 66 MiB is a potential blocker;
coordinate compiler concurrency and preserve receipts before cleaning only own
newly generated unused caches. No source edits/commits or physical GPU claims.
Every case receives PASS, FAIL or NOT RUN with exact limitation, never inferred
success from presence of a test name or developer-focused pass.
