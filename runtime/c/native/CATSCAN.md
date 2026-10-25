# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose pinned WebGPU C entrypoints.

## Authority
Own ABI conversion, adapter/device leases and orchestration, not resource semantics,
callback queues, shader meaning, GPU execution or production defaults.

## Scope
Public objects, compute, graphics, copies and Linux Vulkan presentation.
[Coverage](README.md) separates Vulkan, Metal and Windows D3D12 composition.
Missing backend paths reject.

## Contracts
Pinned `runtime/zig/vendor/webgpu-headers/webgpu.h` and instance, callback,
error-scope, resource, shader and backend contracts apply.
Instance requirements use the owner's [capability amendment](../instance/CAPABILITIES.md).

## Invariants
One tagged device/pending owner selects backend without fallback or migration.
Read only the selected payload. Reflection never substitutes for dispatch analysis.
Unknown work retains resources and progress. Vulkan submit returns at
queue acceptance, settles oldest first, and zero-initializes textures before
queue-ordered access (owner-authorized 2026-10-24). Surface acquisition/discard
uncertainty uses reserved records on the same pending list; presentation requires
its own frame fence. Cleanup-only records do not redefine command WorkDone.
Surface cleanup preserves surface-before-device lock order and undelivered errors.
Invalid surface configuration preserves the active configuration; successful
replacement leaves old frame/view generations on the existing retirement owner.
Provider code remains resident through cleanup; foreign callbacks run outside locks.

## Acceptance
[Owner checks](Makefile) exercise public behavior, rejection, first errors,
rollback and terminal cleanup.
- Evidence: [Source-bound records](README.md) distinguish
host controls, native execution and unqualified paths. Developer results do not
establish independent, physical or performance qualification.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Internal algorithms may vary.
