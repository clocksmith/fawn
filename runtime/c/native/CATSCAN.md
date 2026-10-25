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
Missing paths reject.

## Contracts
Pinned WebGPU ABI and existing owner contracts apply.
Instance requirements use the owner's [capability amendment](../instance/CAPABILITIES.md).

## Invariants
The device/pending owner selects without fallback, reading only
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
Depth transfers and raster state preserve shared admission.
Provider code stays resident; foreign callbacks run outside locks.

## Acceptance
[Owner checks](Makefile) exercise behavior, rejection, errors,
rollback and cleanup.
- Evidence: [Source-bound records](README.md) distinguish
host controls, native execution and unqualified paths.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.

HWND/DXGI composition uses the same native pending submission and resource texture
owners as other backends. Exact backbuffer identity survives early public release;
no surface teardown or resize forces outstanding frame/view leases away. Actual
Windows SDK compilation and window/device execution are separate evidence gates.
