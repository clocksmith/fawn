# CATSCAN: Zig runtime

Parent: [Runtime](../CATSCAN.md)

## Target

Provide Doe's deterministic native WebGPU runtime, compiler, and backend execution in a maintainable Zig implementation.

## Authority

- Owns native API behavior, queues, resources, lifecycle, compiler integration, Metal, Vulkan, D3D12, drop-in, and embedded execution.
- Does not own public package resolution, claim promotion, or external workload truth.

## Scope

- Includes files beneath this directory except child-chartered components, which narrow this authority.

CSL is an owner-authorized optional quarantine, excluded from default native
Zig-to-C parity and installation. Original sources and historical evidence stay
intact; explicit `-Denable-csl=true` and dedicated targets restore access.
The repository `config/csl-quarantine.json` owns the exact boundary.

## Contracts

Inputs:
- Runtime contract: [`README.md`](README.md).
- Source layout policy: [`source-layout.json`](source-layout.json).

Outputs:
- Native libraries, tools, backend artifacts, and typed runtime diagnostics.
- The explicit default-off [C instance seam](../c/instance/CONTRACT.md) keeps its count and queue ownership in C; retained consumers remain native.

## Invariants

- Unsupported capabilities fail with original typed causes and no hidden fallback.
- Backend behavior is real or rejected; fake execution and synthetic state are forbidden.
- Resource ownership, synchronization, completion, and readback remain explicit.
- Compiler analysis owns shader meaning; native objects own WebGPU validation
  and references; backends own platform state and execution.
- Exact program identity and allocation generations authorize reuse. Unknown
  completion retains resources; terminal failure never becomes successful output.
- Compile-time completeness complements behavioral tests, not ownership proof.

## Acceptance

- Core and full runtime contracts pass through the canonical Zig build.
- Evidence: [`build.zig`](build.zig).

## Non-goals

- Reimplementing package policy, benchmark policy, or application-level orchestration.

## Freedom

Any mechanism is permitted if it preserves these boundaries and passes the acceptance evidence.
