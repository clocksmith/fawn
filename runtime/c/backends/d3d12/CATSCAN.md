# CATSCAN: Experimental C D3D12 backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted D3D12 compute, offscreen graphics, transfers and timestamps with
explicit native resource/completion obligations.

## Authority

- Owns adapters, devices, resources, pipelines, commands, queues and fences.
- Owns official DXC invocation and bytecode/module lifetime; shader meaning and metadata remain compiler-owned.
- Excludes WebGPU validation, provider selection and qualification.

## Scope

Private ABI, Windows implementation and fixtures; [guide](README.md).

## Contracts

- Inputs: [typed resources/bytecode](include/doe_d3d12.h), [dynamic bindings](include/doe_d3d12_dynamic.h) or [admitted HLSL/entry/stage/module](include/doe_d3d12_shader.h).
- Outputs: queried identity/caps, statuses and completion-backed readback.

## Invariants

- Unknown completion retains dependencies; release/timeout never settles obligations; destruction refuses unresolved work.
- Production Zig remains intact; no hidden runtime delegation or default change.
- Portable controls prove ownership; SDK CPU controls prove call composition, never driver execution.
- DXC preserves first owned diagnostics and retains its module through bytecode release; no CLI, filesystem includes or source interpretation.
- WARP requires explicit selection/software identity. Failed selection never falls back. CPU evidence never qualifies hardware/public integration.

## Acceptance

- [Owner controls](Makefile) cover rollback, ranges, order and lifetime.
- [SDK CPU controls](tests/test_windows_caps.c) and [DXC control](tests/native_shader.c) create no device.
- [Driver fixtures](tests/native_mesh.c) require separate execution authorization; retained parity and hardware qualification remain separate.
- Evidence: [host receipt](DEVELOPER_RECEIPT_INDIRECT_UNIFORM.json), [SDK/driver scope](README.md).

## Non-goals

Replacing WebGPU or asserting complete graphics, textures, presentation or queries.

## Freedom

Implementation freedom requires these boundaries and acceptance obligations.
