# CATSCAN: Experimental C runtime CLI

Parent: [C migration](../CATSCAN.md)

## Target
Replay host traces and execute command streams through C, producing
doe-zig-runtime-compatible artifacts.

## Authority
Own command-stream parsing, quirk matching, CLI arguments, trace rows/meta and
embedded sample inputs and output-oracle digests, not WebGPU semantics, shader meaning, backend execution,
quirk data or bench comparability policy.

## Scope
Command streams and doe-plan-executor plans through `libwebgpu_doe_c` or
`dawn-c-runtime`, including buffers, textures, compute, graphics, asynchronous
diagnostics and real window-backed surfaces.
[Coverage](README.md) defines admitted and rejected inputs.

## Contracts
`pipeline/trace/replay.py` artifact shape, the trace v1 hash chain, the
command-stream examples and schema 1 output oracles apply.

## Invariants
Unknown commands, fields and arguments reject before device work with exit 2.
Host-only traces create no device and claim no execution. Replay mismatches fail before device work.
No input is silently ignored, emulated or reported as success. Commands preserve the first device error; deferred work drains before successful completion. Oracles run on
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
textures, GPU timestamps or default CLI replacement.

## Freedom
Internal parsing, caching and encoding may vary.
