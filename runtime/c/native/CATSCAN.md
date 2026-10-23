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
textures/views/samplers and copies; requested GPU-normalized timestamps; reflected
vertex/fragment pipelines, color/depth/stencil passes, direct draws, bundles and occlusion.
Unsupported entrypoints stay absent or fail specifically, never fake success.

## Contracts
Pinned `runtime/zig/vendor/webgpu-headers/webgpu.h` and C instance/futures/callbacks,
error-scope, resource, shader and Vulkan owner contracts apply.

## Invariants
No Zig fallback, synthetic completion or device migration. Unknown completion
retains ownership. Foreign callbacks run outside locks. Reflection does not replace
missing dispatch analysis; command and native obligations keep their existing owners.

## Acceptance
Developer checks do not establish independent, physical or performance qualification.
- Evidence: [depth](DEVELOPER_RECEIPT_DEPTH.json), [demote/instance](DEVELOPER_RECEIPT_DEMOTE.json), [graphics](DEVELOPER_RECEIPT_GRAPHICS.json),
  [timestamps/recovery](DEVELOPER_RECEIPT_TIMESTAMPS.json) and
  [admission](DEVELOPER_RECEIPT_ADMISSION.json).

## Non-goals
Parallel resource engine, fake backend, complete graphics or default cutover.

## Freedom
Implementation may change within these owner boundaries.
