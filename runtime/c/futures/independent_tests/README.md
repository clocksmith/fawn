# Independent CPU futures evaluation

Original MIT-licensed evaluator sources. Uses only installed GCC, G++, Python 3,
GNU ar/nm, Linux pthreads and the public C futures API. No Zig invocation/linking,
network, hidden old receipt dependency, sys.path mutation or production runtime
integration. The retained Zig characterization is explicitly separate and is not
an executable oracle for this suite. Preserve LICENSE and source provenance.

## Reproduce once from source

From any directory, use an explicit source root and a new output directory:

```
python3 -B /path/to/suite/run.py --mode plan \
  --source-root /path/to/doe/runtime/c/futures
python3 -B /path/to/suite/run.py --mode full \
  --source-root /path/to/doe/runtime/c/futures --output /path/to/new-evidence
```

The runner independently compiles src/*.c into static archives, builds the C and
restricted C++17 consumers from source, records every command/status/log, hashes
all actual C/header inputs plus contract/provenance, audits exports/undefined
allocation/Zig symbols and records runtime probes. No candidate Makefile or
candidate developer tests are used. Each run refuses an existing output directory.
The full path contains optimized, tiny-test-endpoint, ASan/UBSan and TSan stages.
ASan uses detect_leaks=0 because the evaluated executor is ptraced. This is not
LeakSanitizer evidence. Unsupported sanitizer probes are preserved and explicitly
reported; a successful probe does not mask a later failed instrumented test.

## Prebuilt, public-header-only consumption

Smoke does not need the implementation source. A source root containing only
include/doe_futures.h is sufficient:

```
python3 -B /path/to/suite/run.py --mode smoke \
  --source-root /path/to/public-header-package \
  --archive /path/to/frozen/libdoe_futures.a --output /path/to/new-smoke
```

This builds only evaluator C/C++ consumer objects, links the exact provided
archive, executes both consumers and records its hash/symbol audit. The public
C/C++ record sizes/alignments/offsets agree via abi.c without private headers.
Source attribution for a provided archive must come from its original build
receipt; smoke does not invent that attribution or regenerate the archive.

## Frozen cases and sources

The original frozen inventory and append-only cleanup amendment are included.
They predate candidate inspection; candidate1_admission.md identifies the sealed
public commit and reconciles the final contract hash. Expectations were not
changed after execution. Initial full execution and a small coverage supplement
are separate preserved receipts, not a repeated full campaign.

- functional.c: storage/version/guard failures, identity/non-reuse/registration,
  per-entry wait flags, modes0/1/2/3, ownerless dispatch, ready-before-complete,
  duplicate waits, serial lowest wait selection, issuance boundaries and nested
  pump, callback self-wait/reentry/finish/deinit/free, unfinished callback.
- concurrency.c: actual condition-wait entry handshakes for finite/infinite waits,
  another-thread publication/finish, same-thread versus other-thread completion,
  blocked callback plus concurrent pump/poll, destruction BUSY after last-node
  free, two simultaneously blocked pump callbacks and 1,024 barrier-started IDs.
- selection_reuse.c: concurrent publisher/pump/wait overlap, older-ID readiness
  versus new issuance boundary, mixed-mode serial pump ordering, completion
  deinit/init storage reuse with new IDs. This is the bounded coverage supplement.
- init_faults.c: all synchronization setup acquisition failures, attribute/cond/
  mutex destroy failures, cleanup-only handles, rejection of ordinary calls,
  exact live-member retry accounting and reuse after successful cleanup.
- wait_faults.c: deterministic CLOCK_MONOTONIC deadlines, spurious wakes, clock/
  condition error taxonomy, native time_t overflow, actual finite timeout,
  callback budget consumption with scan-complete precedence and an injected
  ETIMEDOUT with genuine other-thread settlement before lock reacquisition.
- exhaustion.c: agreed test-only endpoints 2 instances, 3 sequences and 3 legacy
  values; explicit saturation/non-reuse. It does not execute billions of calls.
- consumer_c.c, consumer.cpp, abi.c: independent public native consumers, pinned
  public encodings, actual C/C++ layout and static-library C calling convention.

No sleeps prove concurrency. Checked startup/join and condition/barrier happens-
before edges establish the exercised overlap; subprocess deadlines only detect
hangs. TSan acceptance applies only to these observed schedules, not exhaustive
race freedom. Source review separately checks no completion access after dispatch,
queue active-call accounting across wait/callback intervals, callback execution
outside mutex, stop-at-first-error cleanup and retained source/license identity.

## Scope limits

These are new isolated C component consumers. Unchanged production C/WebGPU
consumers have not been migrated or executed against this component. Allocation
here means library-owned allocation, not caller allocation. No payload-allocation,
instance/device-lease, GPU-completion, all-API, production-promotion, performance,
non-Linux, cross-DSO/fork or stale-pointer qualification is claimed. Manual callback
extent after begin_delivery remains caller-owned. Exactly-once registration and
readiness publication remain caller preconditions, not tombstone guarantees.
