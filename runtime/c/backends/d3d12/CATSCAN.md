# CATSCAN: Experimental C D3D12 backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

D3D12 compute/offscreen-graphics/transfers/timestamps preserve resource/completion obligations.

## Authority

- Owns adapters/devices/resources/pipelines/commands/queues/fences.
- Owns DXC/FXC invocation, bytecode/module lifetime; compiler owns meaning/profiles.
- Excludes WebGPU validation, provider selection and qualification.

## Scope

Windows ABI/fixtures; [guide](README.md).

## Contracts

- Inputs: [resources/bytecode](include/doe_d3d12.h), [stages](include/doe_d3d12_render.h), [HLSL/entry/stage/module](include/doe_d3d12_shader.h).
- Outputs: identity/caps/statuses, completion-backed readback.

## Invariants
MapWrite lends zeroed UPLOAD; mapped-at-creation storage retains DEFAULT/shadow.

- Unknown completion retains dependencies/events; release/timeout cannot settle; unresolved destruction rejects.
- Pending ordered buffers forbid CPU access; unlocked observers, context events, serialized retirement.
- DXGI imports retain texture/command leases; COMMON states and producer/release fences preserve ownership.
- Retain Zig intact; no hidden delegation/default change.
- Portable: ownership; SDK: composition, never execution.
- Compilers retain diagnostics/blobs/modules; no CLI, filesystem includes, profile fallback or source interpretation.
- Verify LUID/software identity without retries.
- WARP requires explicit selection/software identity; CPU evidence never qualifies hardware/public integration.
- HWND preserves context/texture/command owners, DXGI backbuffer identity and PRESENT/COPY_DEST transitions.
- Execute precedes Present/signal; only completion/terminal loss retires leases; retained frames block resize.

## Acceptance

- [Owner controls](Makefile) cover rollback/ranges/order/lifetime.
- [SDK](tests/test_windows_caps.c), [DXC](tests/native_shader.c) and [driver](tests/native_mesh.c) retain separate scopes.
- Evidence: [host receipt](DEVELOPER_RECEIPT_INDIRECT_UNIFORM.json), [SDK/driver scope](README.md).

## Non-goals

Complete graphics, textures, presentation, queries or WebGPU replacement claims.

## Freedom

Preserve boundaries/acceptance.
