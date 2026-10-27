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
diagnostics and real window-backed surfaces. Compute and render timing uses real backend timestamp queries. Numeric stability retains explicit annotations,
matmul, RMSNorm and attention capture, evaluation, routing and tensor rewrites.
Explicit matching Dawn delegate lanes preserve textured triangles and real
bundle/binding modes. Native proxy remains default.
[Coverage](README.md) defines admission.

## Contracts
`pipeline/trace/replay.py` artifact shape, the trace v1 hash chain, the
command-stream examples and schema 1/2/3 output oracles apply. Schema 1 uses
isolated buffers; schema 2 replays the command graph with an exact reference;
schema 3 preserves float32 reference-file tolerance and reference classification.

## Invariants
Unknown inputs reject before device work with exit 2.
Host-only traces create no device and claim no execution. Plan dry runs identify
every row as `dry_run`, keep device times/submits zero and omit adapter and
selection results; their retained-compatible row/dispatch counts are nominal.
Replay mismatches fail before device work.
No silent ignoring, emulation or false success. Commands preserve the first device error; deferred work drains before successful completion. Oracles execute outside measured work with their declared initialization and
scope. Reference bytes must match their declared size and digest; exact or
finite float32 tolerance comparisons fail on mismatch. Validation graph adapter
and compiled source identities must match measured execution. Unknown readback
completion retains callback storage; unconfirmed close is explicit in final
metadata and cannot authorize a subsequent GPU workload. Metadata identifies
the actual adapter.

## Acceptance
[Owner checks](Makefile): host parsing, rejection, hash-chain and SHA-256 vectors;
`make check` executes examples, replays artifacts and compares hash chains with
Zig rows.
- Evidence: [Recorded runs](README.md) name adapter and source; CPU adapter
runs are not GPU, performance or comparability evidence.

## Non-goals
Unappliable quirks,
custom render pipelines, multisampled
textures or default CLI replacement.

## Freedom
Internal parsing, caching and encoding may vary.
