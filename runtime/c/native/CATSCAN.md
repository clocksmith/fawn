# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose WebGPU entrypoints.

## Authority
Own ABI conversion and adapter/device leases, excluding resource semantics,
callbacks, compilation, execution and production selection.

## Scope
WebGPU objects and Vulkan/Metal presentation.
[Coverage](README.md) separates Vulkan, Metal and Windows D3D12 composition.
Missing backend paths reject.

## Contracts
Pinned `runtime/zig/vendor/webgpu-headers/webgpu.h` and instance, callback,
error-scope, resource, shader and backend contracts apply.
Instance requirements use the owner's [capability amendment](../instance/CAPABILITIES.md).

## Invariants
One tagged device/pending owner selects backend without fallback.
Read only the selected payload. Reflection never substitutes for dispatch analysis.
Unknown work retains resources. Vulkan submit returns at
queue acceptance, settles oldest first, and zero-initializes textures before
queue-ordered access (owner-authorized 2026-10-24). Surface acquisition/discard
uncertainty uses reserved records on the same pending list; Vulkan presentation
requires its own frame fence. Metal registers presentation with the scheduled handler, retaining drawables through
GPU completion under Apple's release-after-registration contract. Display timestamps
do not govern retirement. Cleanup records do not redefine command WorkDone.
Cleanup preserves surface-before-device locking and undelivered errors.
Invalid configuration preserves surfaces; replacement retains old frames/views
on the existing retirement owner.
Provider code remains resident through cleanup; foreign callbacks run outside locks.

## Acceptance
[Owner checks](Makefile) exercise public behavior, rejection, first errors,
rollback and terminal cleanup.
- Evidence: [Source-bound records](README.md) distinguish
host controls, native execution and unqualified paths. Developer results are unqualified.

## Non-goals
Parallel resource engines, synthetic completion or default cutover.

## Freedom
Algorithms vary.
