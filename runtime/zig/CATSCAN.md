# CATSCAN: Zig runtime

Parent: [Runtime](../CATSCAN.md)

## Target

Provide Doe's deterministic native WebGPU runtime, compiler, and backend execution in a maintainable Zig implementation.

## Authority

- Owns APIs, queues, resource lifetimes, compiler integration, backends,
  drop-in and embedded execution.
- Does not own package resolution, claim promotion or external workload truth.

## Scope

- Includes this directory except child charters.
- [CSL quarantine](../../config/csl-quarantine.json) excludes CSL from default
  parity/installation. Sources and evidence remain; `-Denable-csl=true` enables
  dedicated targets.

## Contracts

Inputs:
- Runtime: [`README.md`](README.md).
- Source layout: [`source-layout.json`](source-layout.json).

Outputs:
- Native libraries, tools, backends and typed diagnostics.
- The default-off [C instance seam](../c/instance/CONTRACT.md) keeps count/queue
  ownership in C; retained consumers remain native.

## Invariants

- Unsupported capabilities retain typed failures; no hidden fallback.
- Instance admission checks selected-owner features/limits before allocation;
  unsupported chains reject without inventing capabilities or changing providers.
- Execution is real; fake execution and synthetic state are forbidden.
- Ownership, synchronization, completion and readback remain explicit.
- Compiler analysis owns shader meaning; native objects own WebGPU validation
  and references; backends own platform state and execution.
- Exact program identity and allocation generations authorize reuse. Unknown
  completion retains resources; terminal failure never becomes successful output.
- Private device policy permanently disables Doe-owned disk caches before use,
  preserves memory caches and advertises no WebGPU capability.
- Compile-time completeness complements behavioral tests, not ownership proof.

## Acceptance

- Canonical core/full contracts pass.
- Evidence: [`build.zig`](build.zig).

## Non-goals

- Package policy, benchmark policy and application orchestration.

## Freedom

Mechanisms must preserve boundaries and pass acceptance checks.
