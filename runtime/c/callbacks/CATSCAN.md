# CATSCAN: Experimental C callback payloads

Parent: [Experimental C migration](../CATSCAN.md)

## Target

Own native callback payload allocation, publication and retirement with explicit event and full-call instance lifetime.

## Authority

- Owns one callback payload allocation, context transfer, event lease and readiness-call lease.
- Does not own future selection, typed API resources, device/backend objects, task scheduling or production selection.

## Scope

- Includes this proposed independent C owner and its default-off retained native adapter contract.

## Contracts

- Inputs: [payload contract](CONTRACT.md), [retained seam](integration/CONTRACT.md), [accepted futures](../futures/CONTRACT.md).
- Outputs: [plain-C API](include/doe_callbacks.h), owned reply lifetime and explicit pre-transfer errors.

## Invariants

- The accepted future queue is the only pending list and identity source.
- Callback/resource cleanup precedes retirement, payload free and event lease release.
- A full-call lease outlives callback-driven payload and last-caller release.
- Failed allocation consumes no identity or reference and leaves context with its caller.
- Retained typed deliverers own API statuses, resources and borrowed pointee lifetimes.
- Default Zig and historical evidence remain intact; CPU rejection-path evidence never qualifies GPU success.

## Acceptance

- Independent C/native tests cover the [planned obligations](CONTRACT.md#acceptance-before-implementation-admission), actual retained consumers and reversible build selection before migration claims.
- Evidence: [migration gates](../../../docs/c-migration/MIGRATION.md).

## Non-goals

- New scheduler, device-loss or successful pipeline ownership, production replacement, GPU execution and performance claims.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
