# CATSCAN: Experimental C Metal backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted Metal compute, graphics, buffer and image operations through a plain C native owner while preserving resource and completion obligations.

## Authority

- Owns experimental Metal loading, native device/queue/buffer/texture/view/sampler/pipeline lifetimes, compute/render pipelines, resolves, visibility/timestamp encoding, copies, submission and retirement.
- Does not own WGSL meaning, public WebGPU validation, provider selection or qualification.

## Scope

- Includes this directory's private backend ABI, native adapter and focused tests.

## Contracts

- Input: validated MSL and explicit compiler workgroup/slot facts through [the compute boundary](include/doe_metal.h) and explicit stage/render facts through [the graphics boundary](include/doe_metal_render.h).
- Output: actual native statuses and completion-backed shared-buffer access.
- Lifecycle: [system invariants](../../../../INTENT.md).

## Invariants

- Pending/unknown completion retains all resource obligations; errors never authorize successful readback.
- No interpreter, hidden backend substitution, Zig delegation or production selection change.
- Host call controls, SDK validation and physical qualification remain separate.
- Native handles are borrowed, and caller serialization is explicit.

## Acceptance

- [Focused checks](Makefile) exercise native-call arguments, rollback, ownership and completion controls.
- Evidence: [current source-bound timestamp developer receipt](DEVELOPER_RECEIPT_TIMESTAMPS.json) records only exercised host scope.
- [Graphics prerequisites developer receipt](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json)
  binds canonical attachment initialization, empty raster-state controls and the
  bounded Apple offscreen scope; it is not public frontend/presentation qualification.
- Apple SDK verification, native device execution and complete retained-consumer parity remain separate gates.

## Non-goals

- Owning WebGPU policy, adding a public lifetime authority or promoting compilation to hardware qualification.

## Freedom

Any implementation is permitted if it preserves these boundaries and passes the acceptance evidence.
