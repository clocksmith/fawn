# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose genuine pinned WebGPU C entrypoints over existing C owners.

## Authority
Own ABI conversion, adapter/device leases and orchestration, not resource semantics,
callback queues, shader meaning, GPU execution or production defaults.

## Scope
Public objects, compute, graphics, copies and Linux Vulkan presentation.
[Coverage](README.md) separates Vulkan, Metal and Windows D3D12 composition.
Missing backend paths reject explicitly, never fabricate success.

## Contracts
Pinned `runtime/zig/vendor/webgpu-headers/webgpu.h` and instance, callback,
error-scope, resource, shader and backend contracts apply.

## Invariants
One tagged device/pending owner selects the backend without fallback or migration.
Read only the selected payload. Reflection never substitutes for dispatch analysis.
Unknown work retains resource leases and progress. Vulkan submit returns at
queue acceptance, settles oldest first, and zero-initializes textures before
queue-ordered access (owner-authorized 2026-10-24). Surface acquisition/discard
uncertainty uses reserved records on the same pending list; presentation requires
its own frame fence. Cleanup-only records do not redefine command WorkDone.
Surface cleanup preserves surface-before-device lock order and undelivered errors.
Provider code remains resident independently of resource cleanup. Foreign callbacks
run outside locks. Existing command/resource owners retain their obligations.

## Acceptance
[Owner checks](Makefile) exercise public behavior, rejection, first errors,
rollback and terminal cleanup.
- Evidence: [Source-bound records](README.md) distinguish
host controls, native execution and unqualified paths. Developer results never
establish independent, physical-matrix or performance qualification.

## Non-goals
Parallel resource engines, synthetic completion, complete graphics or default cutover.

## Freedom
Internal algorithms may vary within these boundaries.
