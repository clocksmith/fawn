# Proposed acceptance obligations (author-written, not independent results)

The reviewed contract/header are installed; freeze a separately authored evaluator
case set against CONTRACT.sha256 before implementation. These obligations are not an independent oracle.
Retain baseline first failures, repaired results, source/build hashes, exact flags,
compiler/libc versions and named executable case counts separately. No build,
test, installation or hardware action has been run for this design.

## Independent semantic cases

Use hand-written expected tuples/counts/byte arrays, not a second stack algorithm.
Run the same event script against retained Zig and candidate C; equality is
necessary but independent expectations remain decisive. Tests must prove that
C symbols were entered, not merely that selected flags compiled.

1. Empty init, each of three filters, clean pop, null-callback pop and empty pop.
   Assert callback count, exact type, null-vs-nonnull message data and length.
2. All 3x5 filter/type pairs plus 0/UINT32_MAX filters, error types 0 and
   UINT32_MAX. Include NoError with a catch-all: it remains uncaptured.
3. Different/same-filter nesting. First matching already-full inner scope absorbs
   later errors without replacement, outer capture or uncaptured dispatch.
4. Depths 0,1,63,64,65, repeated cycles. Overflow emits nothing and does not
   overwrite slot63; exactly 64 successful pops follow 65 pushes.
5. Lengths 0,1,511,512,513 and a much larger borrowed extent; exact first512 bytes,
   embedded NUL, 0xff and a multibyte boundary at511. Both captured and uncaptured
   copies survive immediate source mutation after deliver returns. Zeroed tail
   can be checked only through admitted testing inspection, never out-of-bounds
   callback reads. No test assumes a terminator beyond length.
6. Distinct callback and userdata1/userdata2 sentinels, including either null,
   replacement and clearing. A selected old tuple remains coherent after another
   thread replaces it; no callback/native function is cast to another signature.
7. Pop callback reenters push/deliver/pop on the same stack. Uncaptured handler
   replaces itself, pushes a scope, performs nested delivery/pop and returns.
   Both demonstrate no mutex held across callback. Verify enclosing result is
   a snapshot, not a view of a newly reused slot.
8. Callback can deinit/free its standalone stack and return. Under ASan no
   enclosing pop/deliver/native adapter read is allowed after foreign dispatch.
   No active-count guard or observer reference may mask the last-owner case.
9. Six classifier call sites retain their exact one-owner path. Independently
   enumerate all 24 validation errors, OutOfMemory, at least two unrelated errors,
   and the four surface/compute overrides. This is retained classifier coverage,
   not evidence that classification migrated into C.

## Real boundary fault and threading cases

- No-heap: link wrappers for malloc/calloc/realloc/free and assert zero stack-owner
  calls during requirements/init/push/pop/deliver/set/deinit. Caller allocation
  remains explicit and charged. Never wrap in a way that implements the owner.
- Ordinary validation: NULL stack precedes deliver NULL/nonzero message, which
  precedes known DEAD/cleanup-only rejection. Assert INVALID_ARGUMENT versus
  INVALID_STATE, system_error0, unchanged errno and zero mutex/message/callback
  access. Only exclusive deinit retry recovers cleanup-only storage. Native
  wrappers accept only push OK/LIMIT, pop OK/EMPTY and deliver/set OK; inject every
  other status and require diagnostic termination rather than fabricated success.
- Requirements/init: null output, bad version, null storage, one byte short,
  misalignment, exact requirements and larger storage. Assert precedence,
  untouched storage on preflight rejection, out=NULL on admitted init failure,
  canaries and no mutex-init call before preflight completes.
- Interpose the actual pthread_mutex_init boundary to return a specific error;
  assert original numeric result, no returned live pointer, no destroy on failed
  initialization, and later successful initialization of the same storage.
- Interpose actual lock failure: no pop/push/capture/handler change or callback;
  native wrapper fails closed instead of mapping unexpected status to success.
  Interpose unlock failure AFTER mutation in subprocesses: fatal operation/code
  diagnostic, never retry or claim rollback. Preserve these first failure records.
- Interpose destroy failure: CLEANUP_REQUIRED, same storage retained, normal
  operations INVALID_STATE, no free/reinit, successful exclusive deinit retry.
  Production final release must terminate before freeing a live mutex; process
  termination is not a leak-free cleanup claim.
- errno sentinels on success and returned failure; callback deliberately changes
  errno and its value survives. Capture stdout/stderr only in dedicated tests,
  with no secrets/protected traces.
- Use barriers/latches, not sleep timing, for tuple replacement versus selected
  callback, concurrent first-error competition, and pop versus deliver ordering.
  Assert only source-admitted outcomes; no invented global callback order.
- Four workers use the SAME stack, then explicit join/quiescence before deinit.
  Separately exercise multiple stacks to prove isolation. ThreadSanitizer is its
  own gate if already installed/usable; never report ASan as race proof. Callbacks
  and userdata must themselves obey the stated concurrency lifetime contract.
- Do NOT create an intentionally concurrent free/use race as an API promise;
  external lifetime is a precondition. Test the admissible callback self-free
  case separately and state concurrent destruction is unsupported.

## Actual consumer and ABI cases

- Public-header-only C11 and C++17 translation units link the same GCC archive.
  Check C ABI symbols, archive members, no Zig dependency and no copied future
  queue/payload scheduler. Public C++ excludes exceptions/RTTI in fixture build.
- Compile authoritative header/legacy native callback type checks. Probe all
  fields/alignments described in CONTRACT.md. Execute C->Zig->C callbacks with
  both userdata, type UINT32_MAX, zero/full messages and null callbacks.
  Negative controls alter StringView/result/stack offset expectation and callback
  argument ordering; each must fail before use. A pointer reinterpretation is
  not a repair. Private C type imported by Zig must be real typed storage.
- Build retained default and C-error-scopes-only artifacts from frozen inputs;
  exercise real native Push/Pop/Flat/Set/Inject and public wgpuDevicePopErrorScope.
  Repeat with the admitted C futures+callbacks+instance selection, retaining all
  their original ownership gates. Check no flag/default drift and rollback.
- CPU fixture uses the actual DoeDevice layout and native final-release path,
  explicit scope init, no successful GPU creation claim. Exercise requireAlive's
  exact destroyed diagnostic; actual command-recording OOM/validation and
  immediate-layout rejection. Compare bytes, first errors and resource counts.
- Test standard mode0/3 immediate, mode1 matching wait and mode2 pump through the
  existing owner, including callback final caller release and reentry. Scope
  capture happened synchronously; it must not be duplicated when delivered later.
- Native invalid handle uses a readable wrong-kind object or NULL, not arbitrary
  addresses. Assert legacy future5/INTERNAL versus live empty no-callback.
  Standard empty status3/NoError1/"no error scope to pop" remains distinct.
- Test init failure on the shared production post-create seam with each real
  acquired cleanup obligation represented, without fabricating backend success.
  CPU can exercise actual object/cache-free/no-backend cases; physical successful
  constructors and their cleanup remain unqualified until host access is admitted.
- All 22 current source paths with DoeDevice construction are audited before a
  selected suite claims coverage. Retained generic suites stay default-only;
  selected fixtures explicitly init/deinit rather than silently auto-initialize.
  Importing an existing inline test does not prove its selected execution.
- Node/Bun, standard proc-table clients and hardware receive separate admission.
  The inherited Node preferred-standard-route callback-shape mismatch is NOT fixed by
  passing this native ABI fixture. Record it as downstream debt, not parity.

## Wrong-but-well-typed negative controls

Use disposable source copies, never mutate the frozen candidate or oracle:
FIFO search; last-error overwrite; skip an already-full matching scope; unknown
filter captures NoError; truncate at511; byte strlen; null-callback pop incorrectly skips scope consumption; overflowing push overwrites slot63; swap
userdata; read handler tuple after unlock; callback under lock; read stack after
callback; deinit at logical Destroy; forget final deinit; initialize before final
whole-device assignment; return success after mutex failure; route selected
field back to retained stack. Each control must be killed by a named independent
case. A build rejection only counts for a structural control, not semantic ones.

## Capacity, cost and honest claims

Mandatory capacity is exactly 64 scopes and 512 bytes per message. Proposed
private representation may use NoError as the no-capture sentinel because no
filter captures it; prove that invariant with unknown-type/NoError tests rather
than adding a redundant policy table. A simple entry is filter:u32, type:u32,
length:size_t, bytes[512]; actual layout must be measured, not inferred here.

Before acceptance record GCC sizeof/alignof every private record, total device
extent in both providers, padding, allocation count, per-operation .su stack
usage and the composed consumer/callback frame chain. Provisional host ceilings
for REVIEW are owner storage <=36 KiB and self-owned frame <=2 KiB per operation;
these are budgets, not measurements or permission to drop supported capacity.
At 64 entries the message payload alone is 32768 bytes. Callback recursion remains
caller-controlled and unbounded as before: report frame per level and the tested
reentry depths, never call 64 scopes a total recursion/stack bound. No hidden
allocation, 64-entry whole-stack local copy, VLA or enlarged message limit.

Measure focused baseline/candidate build and test time, clean/incremental cost,
source/headers/private bridge/build closure LOC+bytes, required transitional Zig,
object/archive/DSO/installed bytes and exact dependency provenance. Count the
retained classifier and all active compatibility glue. No final50% or readability
win can be asserted from owner LOC. Report inspected/edited files and debugging
friction only from actual records. C-only sanitizers do not instrument retained
Zig ownership, and no CPU number qualifies physical GPU behavior.
