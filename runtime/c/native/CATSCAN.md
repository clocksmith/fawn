# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose genuine pinned WebGPU C entrypoints over existing C owners.

## Authority
Own typed ABI conversion, adapter/device leases and orchestration, not resource
semantics, callback queues, shader meaning or GPU execution. Defaults stay unchanged.

## Scope
Instances/devices, buffers, compute pipelines, bindings, commands, submission,
readback, device loss, labels, compilation/pipeline callbacks; initialized color
textures/views/samplers and copies, depth/stencil texel copies and textureLoad sampling, cube arrays when enabled, srgb/unorm view formats; requested GPU-normalized timestamps; reflected
vertex/fragment or vertex-only pipelines, color/depth/stencil/MSAA passes, resolve,
direct/guarded-indirect nonindexed and indexed draws, bundles, occlusion and canonical debug-group validation;
Linux Vulkan surfaces (Wayland/XCB/Xlib) with compatibleSurface-enabled WSI and presentation.
Unsupported entrypoints stay absent or fail specifically, never fake success.

## Contracts
Pinned `runtime/zig/vendor/webgpu-headers/webgpu.h` and C instance/futures/callbacks,
error-scope, resource, shader and Vulkan/Metal owner contracts apply.

## Invariants
One tagged device/pending owner selects Vulkan or Metal without fallback.
No Zig fallback, synthetic completion or device migration. Unknown completion
retains ownership and a surviving progress obligation. Surface-only acquisition
and discard uncertainty use a typed reserved record on that same pending list;
presentation commands poll the backend frame fence before release. Cleanup-only
surface records do not redefine command WorkDone readiness. Surface/device
cleanup preserves surface-before-device lock ordering and consumes no undelivered
terminal error. The provider image remains
resident until process exit, independently of terminal GPU/resource cleanup.
Foreign callbacks run outside locks. Reflection does not replace
missing dispatch analysis; command and native obligations keep their existing owners.

## Acceptance
Developer checks do not establish independent, physical or performance qualification.
- Evidence: [budget/platform](DEVELOPER_RECEIPT_BUDGET_PLATFORM.json), [indirect/viewport](DEVELOPER_RECEIPT_INDIRECT.json), [backend bridge](DEVELOPER_RECEIPT_BACKENDS.json), [MSAA/debug/atomics](DEVELOPER_RECEIPT_MSAA.json), [depth](DEVELOPER_RECEIPT_DEPTH.json), [demote/instance](DEVELOPER_RECEIPT_DEMOTE.json), [graphics](DEVELOPER_RECEIPT_GRAPHICS.json),
  [timestamps/recovery](DEVELOPER_RECEIPT_TIMESTAMPS.json) and
  [admission](DEVELOPER_RECEIPT_ADMISSION.json).

## Non-goals
Parallel resource engine, fake backend, complete graphics or default cutover.

## Freedom
Implementation may change within these owner boundaries.
