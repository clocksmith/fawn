# CATSCAN: C Metal backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target
Execute Metal, preserving resource and completion obligations.

## Authority
Own native loading, device/queue/resource/pipeline lifetimes, compute/render,
resolves, visibility/timestamps, drawables, transfers and retirement. Exclude WGSL,
public validation, provider selection and qualification.

## Scope
ABI and tests.

## Contracts
MSL and workgroup/slot facts enter the
[compute boundary](include/doe_metal.h); stage/render facts enter the
[graphics boundary](include/doe_metal_render.h). Outputs: native statuses and completion-backed buffer access. Device dimensions and GPU family bound workgroups; pipelines independently
check maxTotalThreadsPerThreadgroup. Indirect compute copies GPU arguments
to scratch and executes the shared bounds guard,
under [system invariants](../../../../INTENT.md).
Presentation borrows CAMetalLayer, retaining drawables through the texture/command
owner. presentDrawable schedules presentation; GPU completion permits caller-lease
release under Apple's contract, without claiming displayed timestamps.
Indexed/indirect [draws](include/doe_metal_draw_guard.h) use the shared GPU normalizer/scan and existing command owner.

## Invariants
Pending/unknown work retains every resource obligation; failed work never authorizes
successful readback. No interpreter, backend substitution, Zig delegation or
production-selection change. Host, SDK and physical evidence remain distinct. Native handles are borrowed; callers serialize.
Stencil initialization uses parent-format attachments and the speculative aspect
owner, preserving companions.

## Acceptance
[Focused checks](Makefile) exercise native arguments, rollback, ownership and
completion.
- Evidence: [Timestamp records](DEVELOPER_RECEIPT_TIMESTAMPS.json) bind host scope;
[graphics prerequisites](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json) bind initialization, empty raster controls and bounded Apple execution.
Presentation, SDK checks and retained-consumer parity require separate evidence.

## Non-goals
WebGPU policy, duplicate lifetimes or compile-only qualification.

## Freedom
Algorithms vary.
