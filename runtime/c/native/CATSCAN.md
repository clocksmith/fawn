# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose genuine pinned WebGPU C entrypoints over the C owners.

## Authority
Own typed public ABI conversion, adapter/device leases and integration orchestration.
Resource semantics, callback queues, shader meaning and GPU execution remain in
their existing C owners. This is not the production/default runtime.

## Scope
Instance, adapter, device, buffer, compute shader/pipeline, buffer bindings, command
recording, native Vulkan submission and readback; device-loss futures, labels,
compilation diagnostics and compute-pipeline callbacks; initialized color textures,
views/samplers, buffer/image copies and ABI8 image compute bindings; requested
timestamp queries with genuine in-command GPU nanosecond normalization; reflected
vertex/fragment pipelines, single-color passes, direct draws, render bundles and
ordered occlusion queries. Unsupported
entrypoints are absent
or return their API-specific failure. No replacement-completeness claim is implied.

## Contracts
The pinned `runtime/zig/vendor/webgpu-headers/webgpu.h`, accepted instance/futures/
callbacks/error scopes, and authored resource/shader/Vulkan C contracts apply.

## Invariants
No Zig linking or fallback. No synthetic completion. Unknown completion retains
ownership. Native objects never migrate devices. Callback reentry is outside locks.
Shader reflection is not a substitute for missing production dispatch analysis.

## Acceptance
Focused developer build and software-only compute/image/graphics checks are separate from later
independent integrated qualification and blocked physical GPU runs.
- Evidence: [developer graphics checks](DEVELOPER_RECEIPT_GRAPHICS.json),
  [developer timestamp/recovery checks](DEVELOPER_RECEIPT_TIMESTAMPS.json)
  and preceding [admission checks](DEVELOPER_RECEIPT_ADMISSION.json).

## Non-goals
A parallel resource engine, fake backend, full graphics support or default cutover.

## Freedom
Implementation may change while preserving these boundaries and owner semantics.
