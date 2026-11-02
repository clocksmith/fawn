# CATSCAN: C Metal backend

Parent: [C migration](../../CATSCAN.md)

## Target
Execute Metal with resource/completion obligations.

## Authority
Own loading/lifetimes, compute/render, queries, drawables, transfers/retirement.
Exclude WGSL, WebGPU validation/policy, provider selection and qualification.

## Scope
Typed ABI, compressed transfers, whole equal-count multisample copies.
Publish IOSurface format/dimensions before textures; query selected-device/API capabilities.

## Contracts
Compiler inputs: [compute](include/doe_metal.h), [graphics](include/doe_metal_render.h).
Outputs: statuses, completion-backed access, optional nonretiring notifications.
Device workgroups/formats and pipeline maxTotalThreadsPerThreadgroup bound execution.

## Invariants
Leases retain pending/unknown resources; failures forbid readback.
No interpretation, backend/Zig substitution or default changes. Separate host, SDK and physical evidence.
Queue-ordered buffer/texture reuse requires known submitted, directly bound
[tracked resources](https://developer.apple.com/documentation/metal/resource-synchronization);
unsubmitted/unknown/untracked predecessors exclude reuse.
Earlier outcomes precede dependent publication. Host access/eviction requires all uses retired;
unlocked waits hold native leases.
GPU waits retain producer dependencies; export the latest accepted texture-use point.
Generation-bound aspect snapshots preserve discards and companion depth/stencil through
parent-format initialization/native component-copy transfers; publish after completion.
Samples precede timestamp resolves. Native draw state controls depth clip/clamp/bias.
Retain borrowed CAMetalLayer drawables through completion; presentDrawable proves no display timestamp.
Indirect compute guards arguments in GPU scratch; indexed/indirect [draws](include/doe_metal_draw_guard.h)
retain GPU normalizers/scans and command leases.

## Acceptance
[Checks](Makefile): arguments, rollback, ownership, completion.
- Evidence: [timestamps: host](DEVELOPER_RECEIPT_TIMESTAMPS.json),
[graphics: initialization/empty-raster/Apple](DEVELOPER_RECEIPT_GRAPHICS_PREREQUISITES.json).

## Non-goals
WebGPU policy, duplicate lifetimes or compile-only qualification.

## Freedom
Algorithms vary.
