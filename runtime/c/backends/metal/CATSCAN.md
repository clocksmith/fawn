# CATSCAN: C Metal backend

Parent: [C migration](../../CATSCAN.md)

## Target
Execute Metal with resource/completion obligations.

## Authority
Own loading, device/queue/resource/pipeline lifetimes, compute/render,
queries, drawables, transfers, retirement. Exclude WGSL,
public validation, provider selection and qualification.

## Scope
ABI/tests, compressed transfers, equal-count whole multisample copies.
Packed IOSurface import publishes physical format/dimensions before textures.
Capabilities check loaded APIs and selected-device selectors.

## Contracts
MSL/workgroup/slot inputs: [compute](include/doe_metal.h).
Stage/render inputs: [graphics](include/doe_metal_render.h). Outputs: statuses, completion-backed buffer access,
optional nonretiring completed-handler notification. Device capabilities bound workgroups/formats; pipelines
check maxTotalThreadsPerThreadgroup. Indirect compute guards GPU arguments in scratch,
under [invariants](../../../../INTENT.md).
Presentation borrows CAMetalLayer; texture/command leases retain drawables. presentDrawable schedules presentation; GPU completion permits caller-lease
release under Apple's contract, without displayed-timestamp claims.
Indexed/indirect [draws](include/doe_metal_draw_guard.h) use the GPU normalizer/scan and command owner.

## Invariants
Native samples precede timestamp resolves.
Pending/unknown work retains resources; failure forbids successful readback. No interpreter, backend substitution, Zig delegation,
production-selection change. Host/SDK/physical evidence stays distinct. Serialized single-resource owners survive asynchronous acceptance; unlocked waits require native leases.
Stencil initialization uses parent-format attachments/speculative aspect ownership,
preserving companions. Aspect transfers use those bits/native component-copy options,
published after completion. Per-draw native encoder state controls depth clip/clamp/bias.

## Acceptance
[Checks](Makefile) cover arguments, rollback, ownership, completion.
- Evidence: [Timestamps](DEVELOPER_RECEIPT_TIMESTAMPS.json) bind host scope;
[Graphics](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json) bind initialization, empty raster controls and Apple execution.

## Non-goals
WebGPU policy, duplicate lifetimes or compile-only qualification.

## Freedom
Algorithms vary.
