# Label owner candidate 3: recovery qualification

Unaccepted implementation of the frozen ABI 1 contract. The original contract
packet is retained without rewriting its historical design-stage statements.
Independent cases were sealed before this implementation; only their declared
instrumentation interface was read by the author. Independent standalone semantics,
C++ consumption, faults, concurrency and capacity passes were previously observed
on candidate 1; their detailed temporary logs were lost. Fresh independent candidate-3 recovery qualification passes standalone semantics,
C++, faults, concurrency, capacity, sanitizer and producer controls. The real
native four-provider lifecycle comparison remains pending. The combined hybrid
recipe has separate C/C++ no-adapter checks, not that full acceptance.

## Ownership and implementation

`src/store.c` owns one typed open-addressed table with separately allocated byte
copies, a half-full growth threshold and deletion by cluster reinsertion. Key
hashing is only a locator; full pointer-token equality identifies entries.
Overflow preflight precedes growth allocation. Copies and replacement tables are
private until all fallible allocation succeeds. Borrowed label bytes do not move
on table growth. Every allocation/free pair carries the copied allocator tokens,
exact extent and alignment. Unexpected synchronization/alignment failures diagnose
and abort. Exclusive destroy retains all storage after a failed mutex destroy;
closing-state probes reject inertly until the exclusive retry succeeds.

`src/native.c` binds that same store record and implementation to one static
pthread-initialized, libc-allocated, hidden singleton. Its ELF destructor requires
external quiescence, frees remaining labels/table and does not free static control
storage. No registry, object refcount, lazy initialization or public testing hook
is added. Test-only forced symbol-remapping headers can instrument the real
allocation and pthread boundaries without changing the normal source.

## Build and verification

From this directory: `make check BUILD=/tmp/doe-labels-author-20261023/build`.
Outputs are the standalone `libdoe_labels.a` and hidden PIC native
`libdoe_labels_native.a`, with separate JSON receipts. The producer invokes only
existing GCC/GNU tools, with fixed strict flags. It records complete compile
commands, compiler/backend/assembler/linker/archiver identities, source/producer
hashes and actual compiler header dependencies. Verification independently
rediscovers dependency names/hashes, checks exact archive membership/member bytes,
ELF machine/type, hidden owner symbol sets and relevant PIC relocations. Receipt
commands are data, never executed. Native build admission must pass
`--require-native`; instrumented/sanitizer archives are not normal-profile inputs.
Receipts detect stale/mismatched artifacts, not a malicious signer able to forge
all evidence. The independent evaluator supplies the separate adversarial tests.

## Installed integration and inherited bug

`integration/selected-seam.patch` and `PATCH_INPUTS.json` retain the original
reviewed patch inputs. The lead released the shared seam and that patch is now
applied. Its patch/input hashes survive, but the temporary full before/after
snapshots were lost. The facade is selection only;
its adapter explicitly handles statuses and retains the existing three exports.
The new option defaults off, validates the external archive and attaches that
verification to ordinary selected artifacts as well as selected tests. All three
build-options records and the existing independently created selected suites are
included. Browser WASM rejects selection. The original Zig store stays unchanged.
The staged bridge/test files retain their review snapshot; their installed
counterparts now exist under runtime/zig/. Both copies are charged.

`integration/surface-final-label-cleanup.patch` is a separate one-call finalizer
repair. It was applied only after the evaluator reported its preserved old-source
failure: the final surface key incorrectly retained nine bytes while the instance
key cleared. The reviewed surface decision hash was refreshed. Runtime passes on
the repaired source remain pending. No GPU/device/backend success, package/browser adoption, default
replacement or complete migration is claimed.

## Developer evidence and size accounting

Strict C/C++ developer checks exercise copies, aliases, allocator identity and
extent, failure atomicity, growth/deletion, unrelated-key borrowed pointers,
threaded mutation, failed initialization, cleanup retry and fatal pthread errors.
Fresh candidate 3 ASan+UBSan checks pass with LeakSanitizer explicitly disabled
after a newly recorded ptrace-related LSan runtime failure. Fresh separate TSan
checks pass. These new logs are durable; the original candidate 1/2 sanitizer
logs are lost. These are author checks, not independent acceptance or retained-Zig
sanitizer coverage. The original temporary binaries, receipts and manifest files were
lost during an observed executor reset at about 15:59 UTC. Previously reported
results remain observations, not recoverable raw evidence. Candidate 3 starts
fresh qualification from durably snapshotted current sources; its new receipts
and logs are checkpointed to shared evidence, not presented as recovered originals.

Candidate physical lines, including comments/blanks: production C 298; private
layout header 32; public/private API headers 121; Makefile/receipt producer 327;
transitional Zig facade/adapter 54; C/C++ developer tests 356; Python verifier regression 43; staged native test
39. The shared build/inventory patch and generated suite must also be charged at
integration. Patch/staged review copies and docs are additional delivery material,
not silently excluded from repository footprint. Original retained store: 71
physical lines, plus its Zig standard-library map/mutex/allocator implementation.
The C owner adds an independently usable allocator/status/cleanup API and explicit
standalone evidence. This architecture/stdlib/API difference is not a reduction
claim: candidate 1 does not establish the prospective 50% whole-Doe size target.

## Preserved candidate 1 verifier defect

Normal candidate 1 artifacts were observed as genuine PIC, but its direct ELF
inspection accepted a default-PIE object with an external `stderr` PC32 relocation.
A real shared link rejected that object. The compact independent PIC-gap receipt
survives, as does the compact independent old-surface failure receipt. Author/raw
first-failure logs, candidate 1/2 snapshots and archive receipts were lost in the
reset; they are not claimed preserved. Candidate 2, retained by candidate 3, rejects
external-undefined PC32 relocations outside debug sections and adds a regression
that compares genuine PIC with actual default-PIE objects and the real linker.
No store/native/header semantics changed. Fresh candidate 3 developer checks,
including this regression, pass with newly checkpointed receipts and logs. The
evaluator must separately admit the current verifier bytes.

## Candidate 3 recovery boundary

Candidate 3 preserves candidate 2 C, header, producer and test bytes; only this
status document changes. The independent evaluator retained candidate 1 source
hashes and confirms current core/header equality. Original candidate 1/2 manifest
files and binary identities remain lost; reported old manifest digests alone do
not recreate them. A new durable source manifest is sealed before fresh builds.
The separately retained independent surface-failure and PIC-gap summaries remain
original evidence. No heavy native build, GPU, package or default-provider claim
is implied by the recovery checks.
