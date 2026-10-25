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
`upload`, `buffer_write`, `barrier`, WGSL `kernel_dispatch` with buffer and
texture bindings and semantic records, copies, 2D textures, proxy-pipeline draws,
samplers, buffer map/unmap, asynchronous diagnostics, real window-backed surfaces
and doe-plan-executor plans over `libwebgpu_doe_c` or
`dawn-c-runtime`.
[Coverage](README.md) lists admitted and rejected inputs.

## Contracts
`pipeline/trace/replay.py` artifact shape, the trace v1 hash chain, the
command-stream examples and schema 1 output oracles apply.

## Invariants
Unknown commands, fields and arguments reject before device work with exit 2.
No input is silently ignored, emulated or reported as success. Commands complete sequentially and preserve the first device error. Oracles run on
isolated zero-filled buffers; a digest mismatch fails the run. The meta names
the executing adapter, so CPU runs stay identifiable.

## Acceptance
[Owner checks](Makefile): host parsing, rejection, hash-chain and SHA-256 vectors;
`make check` executes examples, replays artifacts and compares hash chains with
Zig rows.
- Evidence: [Recorded runs](README.md) name adapter and source; CPU adapter
runs are not GPU, performance or comparability evidence.

## Non-goals
Numeric stability plans beyond decode logits, unappliable quirk actions,
custom render pipelines, multisampled
textures, GPU timestamps, deferred queue sync or default CLI replacement.

## Freedom
Internal parsing, caching and encoding may vary.
