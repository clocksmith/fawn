# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion, adapter/device leases; exclude resource semantics, callbacks,
compilation, execution, production selection.

## Scope
WebGPU/presentation and queried packed Metal imports; unsupported paths reject.

## Contracts
Pinned ABI, inventoried legacy/browser signatures, [owner contracts](AGENTS.md),
[instance requirements](../instance/CAPABILITIES.md).

## Invariants
Select without fallback; use selected payloads. Windows LUID preserves exact
words/software identity; unknown chains and forced fallback reject. Reflection
never replaces dispatch analysis; unknown work retains resources.
D3D12 indirect guards initialize lazily on the device: compile unlocked, publish
under rechecked device state, and retain private pipelines through command retirement.
Metal/Vulkan acceptance, conflict settlement and initialization share pending ownership.
Presentation retains frame/backbuffer identity and leases across early release,
teardown/resize until backend completion. Display timestamps never redefine
WorkDone. Surface-before-device locking, errors, rollback and retirement ownership persist.
Metal occlusion, calibrated timestamps, unavailable samples, ordered resolves,
depth and raster state retain their owners.
Provider code stays resident; foreign callbacks unlocked.
Persistent-cache opt-out precedes serialized shader/driver access; memory,
caller-owned and opaque driver caches remain separate.
Legacy getters retain leases. Encoded writes snapshot staging with ordered copies.
Unsupported external transforms yield unusable error objects; copies never imply
conversion. Shared IOSurface properties expose physical formats before creation;
compatible views preserve them.

## Acceptance
[Owner checks](Makefile) cover rejection, errors, rollback and cleanup.
- Evidence: [Records](README.md) distinguish host, SDK, native/window and browser qualification.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
