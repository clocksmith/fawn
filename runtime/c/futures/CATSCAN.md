# CATSCAN: Experimental C futures

Parent: [Experimental C migration](../CATSCAN.md)

## Target

Provide independent C instance future identities and completion queues.

## Authority

- Owns issuance, borrowed completion registration, readiness, eligible delivery, waits and retirement.
- Does not own payload allocation, instance/device leases, GPU completion or production selection.

## Scope

- Includes the implementation, public header, default-off native bridge and CPU
  consumer checks.

Private [host primitives](../platform/CATSCAN.md) supply OS operations only.
Semantic and lifetime authority stays here; Win32 source does not extend acceptance.

## Contracts

- Inputs: [ABI and lifetime contract](CONTRACT.md), [experimental consumer seam](integration/CONTRACT.md), [native callbacks](../../../docs/native-callback-contract.md) and [device lifecycle](../../../docs/native-device-lifecycle-contract.md).
- Outputs: [public C header](include/doe_futures.h), explicitly initialized caller-owned queues and observable result statuses.

## Invariants

- IDs never recycle; callbacks run outside synchronization locks.
- Timeout is not retirement; unknown completion cannot justify freeing live state.
- Borrowed payload/node/owner lifetimes remain explicit through callback return and outer call completion.
- No library allocation, hidden Zig dependency or inherited GPU qualification.
- Selected bridge adapts real retained consumers; payload allocation and
  instance/device lease policy remain with their native owners. Mixed-language
  consumer evidence is not a Zig-free runtime claim.

## Acceptance

- Source-bound characterization, native consumption and independent CPU lifetime/concurrency evaluation satisfy the [contract](CONTRACT.md#acceptance-before-migration-claims).
- Experimental integration additionally preserves explicit initialization,
  full-call owner lifetime, cleanup failure and default build reversibility.
- Evidence: [migration gates](../../../docs/c-migration/MIGRATION.md).

## Non-goals

- Production-default selection, all-API parity, asynchronous GPU execution and performance claims.

## Freedom

Implementation freedom requires these boundaries and acceptance evidence.
