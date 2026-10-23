# CATSCAN: Experimental C native frontend

Parent: [C migration](../CATSCAN.md)

## Target
Expose genuine pinned WebGPU C compute entrypoints over the C owners.

## Authority
Own typed public ABI conversion, adapter/device leases and integration orchestration.
Resource semantics, callback queues, shader meaning and GPU execution remain in
their existing C owners. This is not the production/default runtime.

## Scope
Instance, adapter, device, buffer, compute shader/pipeline, buffer bindings, command
recording, native Vulkan submission and readback. Unsupported entrypoints are absent
or return their API-specific failure. No replacement-completeness claim is implied.

## Contracts
The pinned `runtime/zig/vendor/webgpu-headers/webgpu.h`, accepted instance/futures/
callbacks/error scopes, and authored resource/shader/Vulkan C contracts apply.

## Invariants
No Zig linking or fallback. No synthetic completion. Unknown completion retains
ownership. Native objects never migrate devices. Callback reentry is outside locks.
Shader reflection is not a substitute for missing production dispatch analysis.

## Acceptance
Focused developer build and software-only compute checks are separate from later
independent integrated qualification and blocked physical GPU runs.

## Non-goals
A parallel resource engine, fake backend, full graphics support or default cutover.

## Freedom
Implementation may change while preserving these boundaries and owner semantics.
