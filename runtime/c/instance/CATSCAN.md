# CATSCAN: Experimental C native instance

Parent: [Experimental C migration](../CATSCAN.md)

## Target

Own instance allocation, identity, references, completion queue lifetime and cleanup.

## Authority

- Owns the instance's sole allocation/count and queue storage.
- Does not own device/backend execution, typed callback resources, external-texture registry policy or production selection.

## Scope

- Includes the C owner, default-off retained seam and authorized [capability amendment](CAPABILITIES.md).

## Contracts

- Inputs: [ownership and ABI contract](CONTRACT.md), [retained seam](integration/CONTRACT.md), [accepted futures](../futures/CONTRACT.md) and [accepted callbacks](../callbacks/RESULTS.md).
- Outputs: [opaque C boundary](include/doe_instance.h), [GCC owner](Makefile), explicit borrowed views and matched final cleanup.

## Invariants

- A single C atomic count authorizes finalization; no lifetime-owning Zig shell or second queue exists.
- Copied queue views borrow, never silently retain or point to temporary wrappers.
- Full-call leases outlive callbacks that free payloads and release caller references.
- Failed or uncertain synchronization cleanup never authorizes freeing live storage.
- Default Zig, historical evidence and separately qualified owner selections remain available.
- CPU error/unconfigured-surface evidence never qualifies device success or GPU execution.
- Capability reporting, descriptor admission and bounded timed waits agree under the amendment.

## Acceptance

- Independent native/C/C++ lifecycle, fault, concurrency and reversible-build checks satisfy the [requirements](EVALUATION.md).
- Evidence: [bounded accepted results](RESULTS.md), [independent report](independent_tests/FINAL_REPORT.md), and [migration gates](../../../docs/c-migration/MIGRATION.md).

## Non-goals

- Production-default promotion, backend parity, physical GPU qualification, new registry/scheduler or performance claims.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
