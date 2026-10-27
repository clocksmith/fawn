# CATSCAN: Experimental C D3D12 backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted D3D12 compute, offscreen graphics, transfers and timestamps with
explicit native resource/completion obligations.

## Authority

- Owns adapters, devices, resources, pipelines, commands, queues and fences.
- Owns official DXC invocation and bytecode/module lifetime; compiler owns meaning/metadata.
- Excludes WebGPU validation, provider selection and qualification.

## Scope

Private ABI, Windows implementation and fixtures; [guide](README.md).

## Contracts

- Inputs: [resources/bytecode](include/doe_d3d12.h), [dynamic stages](include/doe_d3d12_render.h), [HLSL/entry/stage/module](include/doe_d3d12_shader.h).
- Outputs: queried identity/caps, statuses and completion-backed readback.

## Invariants

- Unknown completion retains dependencies; release/timeout never settles them; destruction refuses unresolved work.
- Retained Zig remains intact; no hidden delegation/default change.
- Portable controls prove ownership; SDK controls prove composition, never driver execution.
- DXC preserves diagnostics and blob/module lifetime; no CLI, filesystem includes or source interpretation.
- LUID discovery queries software identity; ordinary context construction verifies it without retries.
- WARP requires explicit selection/software identity. CPU evidence never qualifies hardware/public integration.
- HWND presentation shares context/texture/command ownership. Exact DXGI backbuffer identity and PRESENT/COPY_DEST transitions persist.
- Execute precedes Present, then signal. Completion/terminal loss alone retires leases; retained frames block resize.

## Acceptance

- [Owner controls](Makefile) cover rollback, ranges, order and lifetime.
- [SDK](tests/test_windows_caps.c), [DXC](tests/native_shader.c) and [driver](tests/native_mesh.c) controls retain separate scopes.
- Evidence: [host receipt](DEVELOPER_RECEIPT_INDIRECT_UNIFORM.json), [SDK/driver scope](README.md).

## Non-goals

Complete graphics, textures, presentation, queries or WebGPU replacement claims.

## Freedom

Mechanisms preserve boundaries and acceptance.
