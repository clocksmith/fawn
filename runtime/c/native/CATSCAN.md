# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion and adapter/device leases; excludes resource semantics,
callbacks, compilation, execution and production selection.

## Scope
WebGPU/presentation, including queried packed Metal imports; unsupported paths reject.

## Contracts
Pinned ABI, inventoried legacy/browser signatures and
[owner contracts](AGENTS.md) apply. Instance requirements follow
[capability amendment](../instance/CAPABILITIES.md).

## Invariants
Select without fallback; read only the selected payload. Windows LUID admission
preserves exact words/software identity; unknown chains and forced fallback reject. Reflection cannot
replace dispatch analysis. Unknown work retains resources. Vulkan acceptance,
ordered settlement and texture initialization share the pending owner.
Presentation retains frame/backbuffer identity and leases across early release,
teardown and resize; only actual backend completion retires them. Display
timestamps never redefine WorkDone. Surface-before-device locking, errors, rollback and retirement ownership persist.
Metal occlusion, calibrated timestamps, unavailable samples, ordered resolves,
depth and raster state retain their owners.
Provider code stays resident; foreign callbacks run outside locks.
Persistent-cache opt-out precedes access, serialized with shader/driver storage.
Memory caches remain; caller-owned and opaque driver caches stay separate.
Legacy getters retain leases. Encoded writes snapshot staging and
record ordered copies. External imports reject unimplemented transforms using
unusable error objects; successful copies cannot imply conversion. Shared IOSurface
properties expose physical format before texture creation; compatible views do not change it.

## Acceptance
[Owner checks](Makefile) cover rejection, errors, rollback and cleanup.
- Evidence: [Records](README.md) distinguish host, SDK, native/window and browser qualification.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
