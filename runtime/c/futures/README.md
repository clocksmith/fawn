# Experimental C future/completion owner

The [contract](CONTRACT.md) and [public header](include/doe_futures.h) are frozen.
This independently buildable owner has passed focused independent CPU acceptance.
Production integration and wider runtime qualification remain separate.
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
