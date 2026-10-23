# CATSCAN: Experimental C Metal backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target
Execute admitted Metal operations through plain C while preserving resource
and completion obligations.

## Authority
Own native loading, device/queue/resource/pipeline lifetimes, compute/render,
resolves, visibility/timestamps, transfers, submission and retirement. Exclude WGSL
meaning, public validation, provider selection and qualification.

## Scope
Private backend ABI, native adapter and focused tests in this directory.

## Contracts
Validated MSL and explicit compiler workgroup/slot facts enter the
[compute boundary](include/doe_metal.h); stage/render facts enter the
[graphics boundary](include/doe_metal_render.h). Outputs are native statuses and
completion-backed shared-buffer access under [system invariants](../../../../INTENT.md).

## Invariants
Pending/unknown work retains every resource obligation; failed work never authorizes
successful readback. No interpreter, backend substitution, Zig delegation or
production-selection change. Host controls, SDK checks and physical qualification
remain distinct. Native handles are borrowed; caller serialization is explicit.

## Acceptance
[Focused checks](Makefile) exercise native arguments, rollback, ownership and
completion.
- Evidence: [Timestamp records](DEVELOPER_RECEIPT_TIMESTAMPS.json) binds host scope;
[graphics prerequisites](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json) bind canonical
initialization, empty raster controls and bounded Apple execution, without
qualifying public presentation. SDK verification, native execution and complete
retained-consumer parity retain separate gates.

## Non-goals
WebGPU policy, another lifetime authority or compilation promoted to hardware qualification.

## Freedom
Internal algorithms may vary within these boundaries.
