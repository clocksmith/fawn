# CATSCAN: Experimental C D3D12 backend

Parent: [Experimental C migration](../../CATSCAN.md)

## Target

Execute admitted D3D12 compute, buffer and admitted texture transfers through an independently usable C owner, preserving native resource and completion obligations.

## Authority

- Owns experimental D3D12 adapter/device, resource, pipeline, command, queue and fence lifetime.
- Excludes WGSL meaning, WebGPU object validation, provider selection and qualification.

## Scope

- Includes this directory's backend-private ABI, Windows implementation and fixtures.

## Contracts

- Input: validated bytecode and typed resource/binding descriptions in [the boundary](include/doe_d3d12.h).
- Output: adapter identity, typed status and completion-backed readback.
- Retained trace and limitations: [owner guide](README.md).

## Invariants

- Unknown completion retains all allocations and dependencies.
- Caller release and timeout are not completion; destruction refuses unresolved obligations.
- Host controls do not simulate shaders or qualify SDK ABI or physical execution.
- Production Zig remains intact, without fallback or default-provider change.

## Acceptance

- [Focused checks](Makefile) exercise rollback, ranges, ordering and lifetime through a private native-call seam.
- [Native fixtures](tests/native_texture.c) requires Windows SDK compilation and separately authorized driver execution.
- Complete retained consumer parity and physical qualification remain separate gates.
- Evidence: [current source-bound receipt](TEXTURE_TERMINAL_RECEIPT.txt); [historical core](TERMINAL_RECEIPT.txt).

## Non-goals

- Replacing WebGPU or asserting complete graphics, textures, presentation or queries.

## Freedom

Implementation is free within these boundaries and acceptance obligations.
