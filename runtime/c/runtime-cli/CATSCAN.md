# CATSCAN: Experimental C runtime CLI

Parent: [C migration](../CATSCAN.md)

## Target
Execute admitted runtime command streams through the C native library and write
doe-zig-runtime-compatible trace artifacts.

## Authority
Own command-stream parsing, quirk matching, CLI arguments, trace rows/meta and
output-oracle digests, not WebGPU semantics, shader meaning, backend execution,
quirk data or bench comparability policy.

## Scope
`upload`, `buffer_write`, `barrier`, WGSL `kernel_dispatch` with buffer
bindings, copies, 2D texture write, query and destroy, proxy-pipeline render
draws and samplers over `libwebgpu_doe_c`. [Coverage](README.md) lists
admitted and rejected inputs.

## Contracts
`pipeline/trace/replay.py` artifact shape, the trace v1 hash chain, the
command-stream examples and schema 1 output oracles apply.

## Invariants
Unknown commands, fields and arguments reject before device work with exit 2.
No input is ignored, emulated or reported as success. Each command completes
before the next starts and preserves its first device error. Oracles run on
isolated zero-filled buffers; a digest mismatch fails the run. The meta records
the adapter that executed, so CPU adapter runs stay identifiable.

## Acceptance
[Owner checks](Makefile): host parsing, rejection, hash-chain and SHA-256 vectors;
`make check` executes example streams, replays their artifacts and compares hash
chains with doe-zig-runtime rows.
- Evidence: [Recorded runs](README.md) name adapter and source; CPU adapter
runs are not GPU, performance or bench-comparability evidence.

## Non-goals
Quirk actions without a C path, surface or indirect commands, custom render
pipelines, mipmapped or multisampled textures, GPU timestamps, deferred queue
sync or default CLI replacement.

## Freedom
Internal parsing, resource caching and encoding may vary within these boundaries.
