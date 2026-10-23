# Author evaluation requirements, not independent cases

No executable tests or candidate implementation exist for this owner. An
independent evaluator must choose/seal its own observable cases from the reviewed
contract/header and actual baseline before implementation begins. This list is
required scope, not a substituted independent oracle or prewritten test program.

## Smallest decisive checks

1. Standalone strict C caller plus source-built C archive: set binary bytes,
   change/free source, get exact bytes, fail a replacement allocation, verify old
   pointer+bytes survive, remove, verify absent, reuse the same opaque key.
2. Actual native C caller dlopens a source-built DSO, creates a real instance,
   sets/gets binary bytes, releases last reference, queries saved integer key and
   observes absence. Repeat with a surface holding the last instance lease. This
   uses the accepted callback rather than a test substitute.
3. Separate old-source regression labels the unconfigured surface itself through
   wgpuSurfaceSetLabel; retain/release must preserve it until final release, then
   the key must be absent. Demonstrate old-source failure before narrow repair.

## Standalone semantics, ownership and adversarial well-typed inputs

- Validation precedence, unsupported-version unreadable-tail guard, NULL output,
  missing hooks and legal NULL allocator identity tokens. Outputs unchanged where
  specified. Config overwritten after create proves copied allocator identities.
- NULL/absent keys, NULL data with nonzero len, non-NULL data with zero len,
  no-op writes on an existing key, NULL get result/output handling. No data read,
  lock or allocation on no-op set. Bytes include NUL, invalid UTF-8 and high bits;
  no terminator allocation, trimming, strlen, encoding normalization or truncation.
- Distinct keys with common address bits, enough entries to grow/rehash and force
  collisions, remove collision head/middle/tail and reuse keys. Test that unrelated
  growth/removal preserves each still-live label pointer and data. Never use stale
  object pointers as readable objects.
- Source alias to the existing label under exclusion; copy precedes old free.
  Replacement allocates new bytes before releasing old; getters see complete old
  or new values under the admitted synchronization, never half-inserted state.
- Fail every actual allocation ordinal on first set, overwrite and table growth.
  Track all allocator pointer/size/alignment/state/vtable pairs and balance; old
  pointer/bytes unchanged, failed new key absent. Remove/get allocate nothing.
  Overflow must be detected before allocation/read/pointer formation. Capacity
  overflow may use a reduced private build limit, never a public mutation API.
- Two stores with identical keys remain isolated and use their own allocators.
  Repeated create/destroy frees every allocation, including remaining labels.
  C++17 consumer uses the public header and GCC archive without exceptions/RTTI.

## Synchronization and fault recovery

Use real pthread synchronization, not a second map implementation. Independent
barriers fix schedules and observers live outside freed state. Shared-store
concurrent writes/removes exercise collision domains and different keys. Same-key
writers may race; read/dereference happens only after external exclusion or when
writers are quiescent. An invalid read-after-unlock race is NOT a supported test
or evidence of pointer safety. Test reentrant allocator use only as prohibited
contract discussion, not an admitted operation that must avoid deadlock.

Fail control allocation and mutex init separately; no initialized sync resource
or allocation may leak. Inject mutex-destroy failure without performing real
destroy: all allocations retained, ordinary rejection-only probes defined, and exclusive
retry succeeds and frees exactly once. In cleanup state, test set with NULL key,
NULL data/nonzero length and non-NULL data/zero length, plus remove with NULL key:
all return CLEANUP_REQUIRED without locking, reading source or invoking allocator
hooks. Cleanup-state get with NULL out remains INVALID_ARGUMENT first, including
an unreadable store argument that must not be inspected. Cleanup-state get with
valid out and NULL/absent key returns CLEANUP_REQUIRED with pointer/length sentinels
unchanged. NULL-store argument validation still precedes cleanup checks. Rejection
probes do not recover storage; only exclusive destroy retry does. Lock/unlock fault
subprocesses must diagnose and terminate, never report safe continuation or free uncertain state. Isolated
misaligned-allocator failure checks must not silently turn into successful stores.

Normal, ASan+UBSan and TSan builds of the C units have separate exact provenance.
Record sanitizer scope/availability honestly. Native C instrumentation never
qualifies retained Zig refcounts, allocators or complete DSO memory safety.

## Real consumers, isolation and rollback

Independently rebuild at least matched Z (retained labels/instance), L (C labels,
retained instance), I (retained labels plus accepted C future/callback/instance),
and IL (all those C owners plus labels), using identical frozen current sources
and fresh output/cache paths. These four prove the label seam against both real
instance lifetimes, not every unrelated provider combination. F-only/FC-only and
combined drop-in-policy selections need their own compatible consumer checks if
claimed; coordinate heavy native builds and do not race the active route lane.

Run the existing 64-lifetime native_labels.c against all four controls, then new
independent cases for aliasing, no-op/replacement/remove, true C-instance callback
cleanup, two-DSO singleton isolation, matched public ABI/procedure lookup, and the
separately repaired surface release. Prove actual public wgpuSurfaceSetLabel
reaches the same map as generic GetLabel and lifecycle remove. Setters for other
object kinds require source-route inventory plus focused CPU seams when admitted;
no fake GPU objects or GPU-success claim. External-texture/shared-buffer no-op
setters and package device label stubs remain unqualified obligations.

Native allocator fault evidence should use test-only link/build instrumentation
at this owner's real allocation boundary, never public test setter symbols or a
substitute store. A private instrumented native binding can supply external
allocator observations under DOE_LABELS_TESTING; it must not be present in ordinary
archives/DSOs. Freeze that exact mechanism with the independent case seal.

Check archive members, source/header/toolchain/flag receipt, target and ELF hidden
visibility; reject missing, stale, extra/wrong-owner, non-PIC or wrong-profile
archives as declared. Check dlsym/nm/readelf: exactly existing public label exports,
no helper/singleton/test hooks exported, no old label-store linkage in selected
consumer, no C-label dependency when default-off. Two DSOs need actual address-
isolation proof under the admitted loader modes. Quiescent native unload must free
remaining table/labels and destroy sync; load/use/unload/reload must start absent.
Observe this with real allocator instrumentation and destructor retention, not
just OS process-exit reclamation. No live object/API/dereference may cross unload.

## Wrong-but-well-typed controls to be independently chosen

Examples of meaningful rejected mutations: reverse empty-data condition; use wrong
key; strlen/truncate at NUL; retain input instead of copy; free old before new
allocation succeeds; publish an entry before table allocation succeeds; discard
copy on overwrite; miss collision deletion; alias two stores/singletons; omit
one facade import; export/preempt private singleton entrypoints; bypass the C
instance remove-label hook; remove at nonfinal release; omit surface cleanup;
free state after failed mutex destroy; silently ignore unlock failure; permit
stale archive identity; load old Zig label owner on selected path. A rejected
control must be caught by the assertion that tests its actual consequence.

## Evidence and quality

Bind contract/header/cases/source/producer hashes, compiler and linker versions,
flags, dependency/archive membership, exact test commands/results and first
failures. Before claim, recapture actively edited shared build/layout inputs with
an explicit drift delta, never mutate historical accepted manifests. Record
clean/incremental/focused-check wall time, files inspected/edited, owner discovery,
first failure and passing-change time, allocation/request counts, installed bytes
and production/test/transitional source subtotals. No LOC/performance claim from
this 71-line Zig owner comparison: C glue, headers, build code and retained Zig
must be charged. No GPU, complete migration, package or browser claim is possible.
