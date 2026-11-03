# CATSCAN: C resources and recorded commands

Parent: [C compiler and runtime](../CATSCAN.md)

## Target
Own canonical resource admission, retained object identities and immutable command
records consumed by the native frontend and backend lifetime clients.

## Authority
Own logical formats, ranges, bindings, usage scopes, map generations, recording
and submission references. Exclude shader meaning, native execution, callback
scheduling and physical completion decisions.

## Scope
Buffers, textures/views/samplers, layouts/groups, shader and pipeline identities,
query/external-texture admission, compute/render passes, bundles and replay.

## Contracts
[Typed API](include/doe_resources.h), [extended API](include/doe_resources_extended.h)
and [owner map](README.md) define caller allocators, native finalizers, device
leases, borrows, sticky errors and reversible submission admission.

## Invariants
Validate same-device identity and checked ranges before mutation. Adopt native
storage only on success; failures preserve caller ownership and outputs.
Keep one reference authority and canonical named format/usage rules. Immutable
records retain complete binding and pass history. Borrowing adds no lease.
Mapping preserves access mode and generation. Unknown submission retains every
in-flight obligation until its existing completion owner proves termination.

## Acceptance
`make -C runtime/c/resources check` exercises logical admission and lifetimes.
`make -C runtime/c check-vulkan` exercises real software-backend consumers;
physical execution and qualification remain separate.
- Evidence: [owner tests and lifetime clients](Makefile).

## Non-goals
Another renderer, parser, native resource engine or completion queue.

## Freedom
Storage and algorithms vary while preserving these boundaries.
