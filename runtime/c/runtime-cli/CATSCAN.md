# CATSCAN: Experimental C runtime CLI

Parent: [C migration](../CATSCAN.md)

## Target
Replay host traces and execute command streams through C, producing
doe-zig-runtime-compatible artifacts.

## Authority
Own parsing, arguments, quirk matching, embedded samples, traces and output digests;
exclude GPU semantics, backend execution, quirk data and comparability policy.

## Scope
Command streams and doe-plan-executor plans through `libwebgpu_doe_c` or
`dawn-c-runtime`, including buffers, textures, compute, graphics, asynchronous
diagnostics and real window-backed surfaces.
Explicit matching Dawn delegate lanes preserve textured triangles and real
bundle/binding modes. Native proxy remains default.
[Coverage](README.md) defines admission.

## Contracts
`pipeline/trace/replay.py` artifact shape, the trace v1 hash chain, the
command-stream examples and schema 1 output oracles apply.

## Invariants
Unknown inputs reject before device work with exit 2.
Host-only traces create no device and claim no execution. Replay mismatches fail before device work.
No silent ignoring, emulation or false success. Commands preserve the first device error; deferred work drains before successful completion. Oracles run on
isolated zero-filled buffers; a digest mismatch fails the run. Metadata identifies the actual adapter.

## Acceptance
[Owner checks](Makefile): host parsing, rejection, hash-chain and SHA-256 vectors;
`make check` executes examples, replays artifacts and compares hash chains with
Zig rows.
- Evidence: [Recorded runs](README.md) name adapter and source; CPU adapter
runs are not GPU, performance or comparability evidence.

## Non-goals
RMSNorm/attention capture plans, explicit numeric annotations, unappliable quirks,
custom render pipelines, multisampled
textures, GPU timestamps or default CLI replacement.

## Freedom
Internal parsing, caching and encoding may vary.
