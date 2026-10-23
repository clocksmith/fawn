# First C futures owner: accepted isolated CPU component

Component: Experimental C futures. Intent: preserve the actual retained CPU
future-ID/completion-queue behavior, with explicit C storage/status/lifetime
adaptations frozen at `431a5a3dd766ed0e3eb267f586721eb8784a53b9`.
Boundary: standalone C static library; no production selection or Zig deletion.
The [contract](CONTRACT.md) and [provenance](PROVENANCE.md) remain unchanged.

## Evidence and consumer distinction

Independent candidate1 evaluation passed 23 reported behavioral/native groups
across 53 recorded command stages, with no candidate or evaluator failure.
This includes modes 0/1/2/3, ownerless delivery, serial lowest eligible selection,
issuance rather than readiness snapshots, reentrant pumps, duplicate waits,
callback finish/deinit/free before return, self/other-thread delivery, actual
finite/infinite condition waits, 1,024 concurrently issued IDs, overlapping
pumps and publisher/pump/wait handshakes. Timeout never implies retirement.

Fault injection passed every synchronization acquisition/cleanup edge, retry of
cleanup-only handles without double destroy, monotonic deadline/spurious-wake
checks, time overflow, clock/condition errors and ETIMEDOUT/readiness races.
Tiny build-only endpoints exercise saturating namespace exhaustion through the
same transitions, not billions of calls or production mutation hooks.

Strict C11 and restricted C++17 public consumers passed source-built and prebuilt
static-library use, encodings, sizeof/alignment/offset agreement and C linkage.
All 14 public symbols are present. Source review and undefined-symbol audit find
no library allocation or Zig dependency. Caller allocation and pthread internals
are separate. These are new component consumers, **not unchanged production
WebGPU/runtime consumers**. Payload wrappers and instance/device leases remain
owned by their retained Zig modules and have not been migrated by this test.

ASan/UBSan passed; LeakSanitizer is explicitly excluded because the executor is
ptraced. Installed ThreadSanitizer passed its runtime probe and the two actual
independent concurrency executables. Those recorded handshake/barrier schedules
are useful race evidence, not exhaustive race freedom. Native host is Debian 13
Linux x86-64, GCC/G++ 14.2, Python 3.12.14 and pthreads. No other host ABI is proved.

Six unchanged retained-owner Zig tests plus the separate import-root test passed
as baseline characterization. That explicitly separate command is never part of
the C generation/build/link/execution path.

## Reproduction and identity

See [README](README.md) for developer build and [independent suite](independent_tests/README.md)
for source-only reproduction and public-header-only/prebuilt smoke. The portable
suite has 17 payload files / 72,979 bytes, plus its local hash manifest. Lead
verified every copied byte, final-path plan, and C/C++ header-only/prebuilt smoke
against the evaluator's exact optimized archive. No full campaign was duplicated.

Original evaluator suite manifest SHA-256:
`28fcb241290bd8b86ff3d470888d87394b2484f79ceb5f921210bb22bbe63302`.
Independent acceptance summary SHA-256:
`4aac73d1f58cfb56ef9aa2c13d7a49585573a9dd0120cd77f3ae1b4c6d8449d4`.
Optimized evaluator archive SHA-256:
`bb2f3ad163e144e154814f2a79e7b083d6008f8f0b8b123b2287eef393aac42e`.

Owner source SHA-256:

- `src/futures.c`: `803bfe03c34216d16145b24bbdb017e064a572a629fd6fdd54faa367b38fda70`
- `src/storage.c`: `b3750c5acdb54f2d3e6e175227cab3de541c1557e10c5b872cb1e5894c3b2fd9`
- `src/internal.h`: `5c243d8599b0b4326d90c45ac45790529f97c0f8d3854b0b4f4aaa759d60099e`

The isolated evidence root is `futures1/` under the campaign evidence directory:
`evaluator/ACCEPTANCE_SUMMARY.json`, `candidate1/results.json`, supplemental
`candidate1-supplement/results.json`, and `lead/prebuilt-smoke/results.json`.
These bind exact commands, flags, actual inputs, exits and sanitizer schedules;
portable tests do not depend on those local receipt paths.

## First failures, cost and remaining gates

The first developer build rejected nonstandard `_Alignof(expression)` under
strict C11; it was repaired to `_Alignof(type)` before candidate1. The initial
LeakSanitizer run failed due to ptrace; its receipt was preserved and only that
unsupported checker was excluded. No developer semantic failure was observed;
this does not imply untested behavior is correct. Candidate1 stayed unchanged
during independent evaluation. SYSTEM_ERROR is not transactional rollback after
readiness publication/retirement; ownership preconditions remain explicit.

Developer process observations: 536 private C/header lines / 20,488 bytes,
62,810-byte optimized/debug archive; one clean build 0.1845 seconds and one no-op
build 0.00195 seconds. These are single local observations, not matched language
comparisons, performance advantages, lower lifetime cost or maintenance proof.

Exactly-once registration/publication, valid borrowed storage, external quiescence
and manual callback lifetime remain caller obligations. No stale-pointer,
cancellation, fork/cross-DSO, all-API, payload/lease, production integration or GPU
completion claim is made. Production Zig replacement count remains zero. The
next runtime owner must independently qualify actual payload/instance/device
lifetime integration before any production selection is considered.
