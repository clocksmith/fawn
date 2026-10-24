# CATSCAN: C host primitives

Parent: [C migration](../CATSCAN.md)

## Target

Provide private, typed CPU synchronization, thread-handle and monotonic-time
primitives for the existing C runtime owners on POSIX and Win32.

## Authority

Owns direct native operations and partial primitive construction/cleanup.
Scheduling, queues, reference counts, device leases, callbacks and completion
policy remain with their existing owners.

## Scope

Includes the shared private headers and focused host CPU controls. Native platform
selection remains a build choice; consumer state machines remain outside.

## Contracts

Inputs and lifetime obligations: [primitive contract](README.md).
Outputs: [private header](include/doe_platform.h), original native errors,
distinct timeouts and retained failed cleanup obligations.

## Invariants

- Use actual pthread/Windows SDK types and functions; no handwritten platform ABI.
- No allocation, emulated pthread API, scheduler or second resource owner.
- Timeout and detach never establish callback completion or permit early free.
- Recursive condition waits require exactly one lock acquisition.
- Preserve original POSIX fault boundaries and retryable partial cleanup.

## Acceptance

The shared CPU control runs on each claimed host. GNU boundary fault controls
and unchanged consumer fault/time tests preserve their declared contracts.
SDK compilation, CPU execution and physical backend execution remain distinct.
- Evidence: [shared CPU control](tests/test_primitives.c), [fault control](tests/test_posix_faults.c)
and [build routes](Makefile).

## Non-goals

Public WebGPU/D3D12 integration, GPU qualification, provider selection and
production-default changes.

## Freedom

Any direct native implementation is permitted if it preserves this boundary and
the calling owners' existing behavior and cleanup contracts.
