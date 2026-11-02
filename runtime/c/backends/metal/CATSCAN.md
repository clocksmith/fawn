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
Packed IOSurface publishes physical format/dimensions before textures.
Capabilities check loaded APIs and selected-device selectors.

## Contracts
MSL/workgroup/slot inputs: [compute](include/doe_metal.h).
Stage/render inputs: [graphics](include/doe_metal_render.h). Outputs: statuses, completion-backed buffer access,
optional nonretiring completion notification. Device workgroups/formats and pipeline maxTotalThreadsPerThreadgroup bound execution. Indirect compute guards GPU arguments in scratch,
per [invariants](../../../../INTENT.md).
Presentation borrows CAMetalLayer, retaining drawable leases through GPU completion;
presentDrawable schedules without display-timestamp claims.
Indexed/indirect [draws](include/doe_metal_draw_guard.h) use the GPU normalizer/scan and command owner.

## Invariants
Native samples precede timestamp resolves.
Producer dependencies retain GPU waits; exports reference committed queue timeline values.
Pending/unknown work retains resources; failure forbids successful readback. No interpreter, backend substitution, Zig delegation,
production-selection change. Host/SDK/physical evidence stays distinct. Command-owned buffer leases admit queue-ordered reuse after known submission;
unsubmitted/unknown owners exclude reuse. The ordinary queue synchronizes directly bound
[tracked resources](https://developer.apple.com/documentation/metal/resource-synchronization).
Earlier failure/completion precedes dependent publication,
host access and eviction require all uses retired; unlocked waits require native leases.
Stencil initialization uses parent-format attachments/speculative aspect ownership,
preserving companions. Aspect transfers use those bits/native component-copy options,
published after completion. Per-draw native encoder state controls depth clip/clamp/bias.

## Acceptance
[Checks](Makefile) cover arguments, rollback, ownership, completion.
- Evidence: [Timestamps](DEVELOPER_RECEIPT_TIMESTAMPS.json) bind host scope;
[Graphics](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json) bind initialization/empty raster/Apple execution.

## Non-goals
WebGPU policy, duplicate lifetimes or compile-only qualification.

## Freedom
Algorithms vary.
