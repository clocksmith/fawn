# CATSCAN: C runtime CLI

Parent: [C migration](../CATSCAN.md)

## Target
Replay traces/commands through C with doe-zig-runtime-compatible artifacts.

## Authority
Own parsing, arguments, quirk matching, embedded samples, traces/digests;
exclude GPU semantics, backend execution, quirk data and comparability policy.

## Scope
Streams/plans through `libwebgpu_doe_c`/`dawn-c-runtime`: buffers, textures,
compute, graphics, asynchronous diagnostics, real windows and timestamp queries.
Numeric annotations, matmul/RMSNorm/attention capture/evaluation/routing/rewrites;
matching Dawn lanes retain textured triangles and bundle/binding modes.
Native proxy defaults; [coverage](README.md) defines admission.

## Contracts
`pipeline/trace/replay.py` shapes, trace-v1 chains and stream examples apply.
Oracle schemas: 1 isolated buffers; 2 exact graph replay; 3 float32 reference-file
tolerance/classification.

## Invariants
Unknown inputs exit 2; replay mismatches fail before device work.
Host-only traces create no device/claim execution. Dry-run rows say `dry_run`:
zero device times/submits, no adapter/selection results, nominal row/dispatch counts.
No silent ignoring, emulation or false success. Preserve first device error;
drain deferred work before success. Oracles run outside measurement with declared
initialization/scope. Reference size/digest and exact/finite-float32 comparisons
must match. Validation graph adapter/compiled source match measurement.
Unknown readback retains callback storage. Final metadata names actual adapter
and unconfirmed close; that close cannot authorize subsequent GPU work.

## Acceptance
[Checks](Makefile): parsing, rejection, hash-chain/SHA-256 vectors; `make check`
executes examples, replays artifacts and compares Zig chains.
- Evidence: [Receipts](README.md) identify adapter/source; CPU runs prove no GPU/performance/comparability.

## Non-goals
Unappliable quirks, custom render pipelines, multisampling, default CLI replacement.

## Freedom
Parsing/caching/encoding may vary.
