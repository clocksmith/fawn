# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion and adapter/device leases, excluding resource semantics,
callbacks, compilation, execution and production selection.

## Scope
WebGPU and presentation. [Coverage](README.md) separates Vulkan, Metal and D3D12.
Missing paths reject.

## Contracts
Pinned ABI, inventoried legacy/browser signatures and
[owner contracts](AGENTS.md) apply. Instance requirements follow the
[capability amendment](../instance/CAPABILITIES.md).

## Invariants
Select without fallback; read only the selected payload. Reflection cannot
replace dispatch analysis. Unknown work retains resources. Vulkan acceptance,
ordered settlement and texture initialization share the pending owner.
Presentation retains frame/backbuffer identity and leases across early release,
teardown and resize; only actual backend completion retires them. Display
timestamps never redefine WorkDone. Surface-before-device locking, errors, rollback and retirement ownership persist.
Metal occlusion, calibrated timestamps, unavailable samples, ordered resolves,
depth and raster state retain their owners.
Provider code stays resident; foreign callbacks run outside locks.
Persistent-cache opt-out precedes first access, serialized with shader
storage and driver-cache configuration. Memory caches remain; independent
caller-owned caches and opaque driver caches stay separate.
Legacy getters retain leases. Encoded writes snapshot ordinary staging and
record ordered copies. External imports reject unimplemented transforms using
unusable error objects; successful copies cannot imply color conversion.

## Acceptance
[Owner checks](Makefile) cover rejection, errors, rollback and cleanup.
- Evidence: [Source-bound records](README.md) distinguish host controls,
SDK compilation, native/window execution and browser qualification.
ABI agreement cannot qualify browser/GPU semantics.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
