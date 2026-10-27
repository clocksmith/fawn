# CATSCAN: C Metal backend

Parent: [C migration](../../CATSCAN.md)

## Target
Execute Metal with resource/completion obligations.

## Authority
Own native loading, device/queue/resource/pipeline lifetimes, compute/render,
resolves, visibility/timestamps, drawables, transfers and retirement. Exclude WGSL,
public validation, provider selection and qualification.

## Scope
ABI/tests, compressed transfers and equal-count whole multisample copies.
Packed IOSurface import publishes physical format and dimensions before textures.
Capability discovery checks loaded APIs and selected device selectors.

## Contracts
MSL and workgroup/slot facts enter the
[compute boundary](include/doe_metal.h); stage/render facts enter the
[graphics boundary](include/doe_metal_render.h). Outputs: statuses and completion-backed buffer access. Device capabilities bound workgroups and formats; pipelines
check maxTotalThreadsPerThreadgroup. Indirect compute guards GPU arguments in scratch,
under [invariants](../../../../INTENT.md).
Presentation borrows CAMetalLayer; texture/command leases retain drawables. presentDrawable schedules presentation; GPU completion permits caller-lease
release under Apple's contract, without claiming displayed timestamps.
Indexed/indirect [draws](include/doe_metal_draw_guard.h) use the GPU normalizer/scan and command owner.

## Invariants
Timestamp resolves require native sample publication.
Pending/unknown work retains resources; failed work never permits successful readback. No interpreter, backend substitution, Zig delegation or
production-selection change. Host, SDK and physical evidence remain distinct. Callers serialize borrowed handles.
Stencil initialization uses parent-format attachments and the speculative aspect
owner, preserving companions. Aspect transfers use those bits and native
component-copy options, published after completion. Per-draw depth clip/clamp
mode and bias use native encoder state.

## Acceptance
[Checks](Makefile) exercise native arguments, rollback, ownership and
completion.
- Evidence: [Timestamps](DEVELOPER_RECEIPT_TIMESTAMPS.json) bind host scope;
[Graphics](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json) bind initialization, empty raster controls and Apple execution.

## Non-goals
WebGPU policy, duplicate lifetimes or compile-only qualification.

## Freedom
Algorithms vary.
