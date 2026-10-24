# Experimental C future/completion owner

The [contract](CONTRACT.md) and [public header](include/doe_futures.h) are frozen.
The original Linux owner passed focused independent CPU acceptance at the
source identities in RESULTS.md. The later Darwin source-build extension below
has focused developer controls only. Production and wider runtime qualification
remain separate.
[Provenance](PROVENANCE.md) binds the retained source; CONTRACT.sha256 seals the
native boundary. The [migration gates](../../../docs/c-migration/MIGRATION.md)
retain the production-promotion requirements.

This is the actual future-ID/CPU completion owner, not a toy counter. Payload
allocation, instance/device leases and physical GPU completion remain outside
this library. Production Zig is unchanged. No C command invokes or links Zig.

## Build and native use

From the repository root, with existing GCC, G++, binutils and pthreads:

```
make -C runtime/c/futures all
make -C runtime/c/futures check
```

The static library is `runtime/c/futures/build/libdoe_futures.a`. Include
`runtime/c/futures/include/doe_futures.h` and link the library plus `-pthread`.
The [C++ consumer](tests/test_cpp.cpp) uses the same C header and library with
exceptions and RTTI disabled. Requirements must be queried from the library
build being used, not cached across builds. Ordinary malloc storage meets this
build's fundamental alignments; callers must honor the actual returned alignment.

Queue/completion storage is caller-owned; init/deinit are explicit. A completion
is borrowed from its payload owner. Registered callbacks call finish after
foreign callback/resource cleanup, then may deinit/free the completion. The
caller must keep the queue alive through the enclosing library call, even if a
callback already finished/freed the last node. See the contract for external
quiescence and manual-delivery lifetime obligations.

Ordinary failed init returns no handle and leaves storage reusable only after
completed cleanup. CLEANUP_REQUIRED instead preserves a cleanup-only queue
handle; retain storage and retry only exclusive queue_deinit. Failed destroys
never silently clear ownership flags. BUSY preserves a live queue.

SYSTEM_ERROR is not a transactional-rollback promise. In particular, a failed
condition broadcast after readiness publication or finish does not undo that
published state/retirement. Normal mutex failure indicates violated lifetime or
platform invariants, not a recoverable cancellation protocol. Callback errors,
nonlocal exits and thread cancellation are outside this boundary.

## Focused developer checks

`make check` runs separate native executables for:

- C storage, issuance, duplicates, unknown IDs, retirement, modes, ordering,
  duplicate waits, reentrant issuance/readiness and recursive pump behavior.
- Restricted C++ public-header/static-library consumption.
- Setup and destruction faults, repeated cleanup-only recovery and storage reuse.
- Build-only tiny ID endpoints through the unchanged saturation transitions.
- One monotonic deadline across synthetic spurious wakes, arithmetic overflow,
  real finite wait and readiness racing an ETIMEDOUT result.
- Real condition-variable startup, indefinite wake/delivery and 20 repeated
  concurrent-pump handshakes. Callback lifetime tests free nodes before return.

Fault tests use linker wrapping of real pthread/clock boundaries, not a second
queue implementation. Thread startup/joins are checked; conditions establish
ordering, not sleeps. `tests/characterize_zig.py` is an explicit separate baseline
command described in provenance, never a C build dependency.

Developer checks are not independent acceptance. The focused developer suite
also passed ThreadSanitizer, including the condition-handshake concurrency paths.
A local AddressSanitizer and
UndefinedBehaviorSanitizer pass excludes LeakSanitizer where the execution
sandbox reports its ptrace incompatibility. It does not establish leak-tool
coverage, physical GPU behavior, production migration or performance advantage.

## Independent reproduction

The [portable evaluator](independent_tests/README.md) builds from source without
using the developer Makefile or tests. From the repository root:

```
python3 -B runtime/c/futures/independent_tests/run.py --mode plan \
  --source-root runtime/c/futures
python3 -B runtime/c/futures/independent_tests/run.py --mode full \
  --source-root runtime/c/futures --output /path/to/new-evidence
```

Use a new output directory. Its separate smoke route consumes only the public
header and an exact prebuilt archive. The exercised ABI is Linux x86-64 with
GCC/G++ 14.2, C11/C++17 and pthreads; Windows/32-bit/cross-DSO compatibility is
not established. See [results](RESULTS.md) for source-bound scope and limitations.

## Darwin source-build extension

The original frozen contract/receipts remain Linux evidence. The synchronization
owner now also admits Darwin pthreads in source. It retains the same queue,
identity, callback, active-call and cleanup state machine. No public ABI record
changes; callers still query the current platform library's storage requirements.
Darwin has no `pthread_condattr_setclock`, so the native condition uses its default
attributes and finite waits call `pthread_cond_timedwait_relative_np`. Remaining
time is recomputed from the one checked absolute `CLOCK_MONOTONIC` deadline after
every wake, including nanosecond borrowing. It never restarts the original timeout
or uses wall-clock deadlines. ETIMEDOUT still returns timeout without a readiness
rescan; a subsequent call can observe the race. Scheduler delays and callbacks
are not bounded execution-time promises; suspend/resume behavior is not qualified.

Apple's declaration and implementation are inspected, not vendored:
- https://github.com/apple-oss-distributions/libpthread/blob/main/include/pthread/pthread.h
- https://github.com/apple-oss-distributions/libpthread/blob/main/src/pthread_cond.c

`make check` retains the Linux behavior/fault/real-condition tests. On a Linux
host, `make check-darwin-boundary` additionally compiles the real Darwin branch
under the private testing guard and substitutes only its typed condition call and
clock. It checks decreasing spans, borrowed seconds, timeout0, spurious wakes,
platform error, readiness-timeout race and no unavailable clock-attribute call.
This is not a Darwin runtime, SDK or scheduling test. No Apple SDK was installed.
`make all CC=clang AR=ar` is the intended Darwin archive build; ordinary C/C++
consumer checks may use `make check-portable`. GNU linker-wrapped controls remain
Linux-only. Native Mach-O library/package linking is owned separately.

## Shared host primitive source extension

Current source uses the private [host primitive owner](../platform/README.md) for
mutexes, conditions, thread identity and monotonic time. The queue still owns
IDs, registration, active calls, callback delivery, timeout/readiness ordering
and cleanup state. Linux and Darwin use the same real pthread/clock symbols as
the retained fault controls; the Darwin test guard selects only its relative-wait
call boundary. The Win32 leaf uses actual SDK synchronization and QPC deadlines,
with no new queue or scheduler. Platform-specific opaque storage sizes must be
queried from the selected build. The public ABI declarations remain unchanged.

`make check-portable` is the C/C++ API route without GNU linker wrapping. GNU
`make check check-darwin-boundary` remains the full focused Linux owner gate. The
shared primitive's CPU control is separate from this queue's behavioral gate.
Source portability does not extend the historical Linux receipts, qualify suspend
behavior or establish Windows public-frontend/GPU acceptance.
