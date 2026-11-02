# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion/adapter/device leases; exclude resource semantics, callbacks,
compilation, execution, production selection.

## Scope
WebGPU/presentation, queried IOSurface/Vulkan-fd/DXGI imports; others reject.

## Contracts
Pinned ABI, inventoried legacy/browser signatures, [owner contracts](AGENTS.md),
[instance requirements](../instance/CAPABILITIES.md).

## Invariants
Select without fallback. Windows LUID preserves words/software identity;
unknown chains/forced fallback reject. Reflection never replaces dispatch
analysis; unknown work retains resources. Metal waits/notifications share
the completion mutex.
D3D12 guards compile lazily unlocked, publish after device revalidation, retain through retirement. Acceptance, conflict
settlement and initialization share pending ownership; held event observers run unlocked.
Presentation retains frame/backbuffer identity and leases across early release,
teardown/resize until backend completion. Display timestamps never redefine
WorkDone. Surface-before-device locking, errors, rollback and retirement ownership persist.
Metal occlusion, calibrated timestamps, unavailable samples, ordered resolves,
depth and raster state retain their owners.
Provider resident; serial/generation futures return immediately; foreign callbacks unlocked.
Cache opt-out precedes serialized access; shader profiles bind persisted artifacts;
memory/caller/driver caches stay separate. Uncertified/imported HLSL retains SM6.
Getters retain leases; writes snapshot staging with ordered copies.
Unsupported external transforms yield unusable error objects; copies never imply
conversion. Shared IOSurface properties expose physical formats before creation;
compatible views preserve them.

## Acceptance
[Owner checks](Makefile) cover rejection, errors, rollback and cleanup.
- Evidence: [Records](README.md) distinguish host, SDK, native/window, browser qualification.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
