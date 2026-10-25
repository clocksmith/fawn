# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion and adapter/device leases, excluding resource semantics,
callbacks, compilation, execution and production selection.

## Scope
WebGPU and presentation.
[Coverage](README.md) separates Vulkan, Metal and Windows D3D12 composition.
Missing backend paths reject.

## Contracts
Pinned WebGPU ABI and existing owner contracts apply.
Instance requirements use the owner's [capability amendment](../instance/CAPABILITIES.md).

## Invariants
The tagged device/pending owner selects without fallback and reads only
its payload. Reflection never substitutes for dispatch analysis.
Unknown work retains resources. Vulkan queues return at acceptance, settle oldest
first, and zero-initialize textures before queue-ordered access (authorized 2026-10-24).
Surface uncertainty uses the same pending list; Vulkan presentation requires frame
fences. Metal registers presentation with the scheduled handler, retaining drawables
through GPU completion.
Display timestamps never govern retirement; cleanup never redefines WorkDone.
Cleanup preserves surface-before-device locking and errors. Invalid configuration
preserves surfaces; replacement retains frames/views on the existing retirement owner.
Metal occlusion resolves real visibility buffers under command leases; passes reset
only begun indices, preserving other results.
Metal timestamps use calibrated GPU nanosecond conversion; ordered writes preserve
prior resolves, unavailable samples become zero, and native errors remain sentinels.
Depth/stencil copies preserve canonical aspects and initialization.
Provider code stays resident; foreign callbacks run outside locks.

## Acceptance
[Owner checks](Makefile) exercise public behavior, rejection, first errors,
rollback and terminal cleanup.
- Evidence: [Source-bound records](README.md) distinguish
host controls, native execution and unqualified paths. 

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
