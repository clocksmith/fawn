# Tiled Spatial IR contract

TSIR separates recovered WGSL meaning from a target-specific realization.
This page defines that boundary, not a kernel migration order or another
implementation queue. [GOALS.md](../GOALS.md) selects work;
[TSIR status](status/tsir.md) links current blockers and receipts.
The [original design and implementation history](https://github.com/doerun/doe/blob/82145070d5f51b0c887270fa9f907df2921cf354/docs/tsir-lowering-plan.md) preserves the former
steps, scope decisions, and their evidence.

## Ownership

```text
Doppler program / WGSL -> Doe frontend -> semantic TSIR
  -> target descriptor + planner -> realization TSIR -> backend emitter
  -> HostPlan / native execution -> output and parity receipt
```

Doppler owns model semantics, manifests, weights, tokenization, and reference
exports. Doe's WGSL frontend owns shader semantic recovery. TSIR owns explicit
kernel lowering identity. Backend emitters realize that identity; HostPlan owns
launches, runtime I/O, and receipt orchestration. Neither replaces Doppler's
program contract or Doe's existing WGSL IR.

## Semantic and realization identity

`tsir.semantic` describes backend-independent meaning:

- loop nests, iteration axes, and analyzable affine bounds;
- buffer bindings, logical shapes, and access summaries;
- reduction regions, explicit barriers, and collective nodes;
- numerical contract and source WGSL/IR identity;
- family hints that may guide analysis but never select a hidden emitter.

`tsir.realization` describes the selected target implementation:

- tile factors, PE-grid shape, and buffer residency;
- collective mappings, reduction trees, and emitter parameters;
- target descriptor, planner, and emitter identity.

Both are canonical, schema-backed and hashed. Semantic identity is scoped to a
pinned frontend version; a frontend change that alters semantic TSIR is an
explicit identity migration, not silent reuse. Realization changes invalidate
its lowering identity without rewriting the source program.

Target descriptors separate correctness-affecting capabilities from planner
hints. Supported dtypes, collectives, lane width, memory limits, and numerical
properties enter realization identity. Latency, bandwidth, and cost hints guide
search without pretending to change source meaning.

## Frontend and planning

The frontend recovers induction variables, normalized control flow, affine
access/dependence information, reductions, bindings, and subgroup semantics.
Unsupported recovery rejects with a typed reason instead of substituting a
classifier-selected template.

Planning explicitly assigns replicated, sliced, streamed, or recomputed
residency. Allocation accounts for persistent buffers, temporaries, parameters,
collective scratch, fabric resources, and reserved memory. Hardware limits are
target data. Fit failure identifies the attempted realization and does not
establish impossibility for every implementation on that hardware.

Barriers become explicit synchronization. Subgroup broadcasts, ballots,
shuffles, reductions, and scans declare lane mapping and ordering. A collective
that cannot preserve its contract on a target is rejected.

## Numerical and oracle contract

Freeze inputs, shapes, weights, source, reference, target, and exactness class
before parity execution. The declared class controls acceptance:

- `bit_exact_solo` requires the declared exact byte result.
- `algorithm_exact` pins reduction order, tree shape, accumulation dtype, and
  allowed grouping; operation-family agreement alone is insufficient.
- `tolerance_bounded` names the metric and tolerance before evaluation.

Declare NaN/Inf propagation and transcendental policy. Pinned-libc bootstrap
references and tolerance-bounded transcendental cases retain their original
scope. Stronger arithmetic or a new reference receives a new receipt; it cannot
retroactively promote an old result or promise universal cross-GPU bit identity.

The bootstrap scalar interpreter is an independent program for the declared
`fused_gemv`, `rms_norm`, and `gather` catalog. Historical real-kernel CSL lanes
compare against a frozen `doppler.reference-transcript/v1` on the same inputs.
That comparison establishes agreement with that reference, not independent
source-model accuracy or general backend correctness. A workload requiring an
independent mathematical/source reference must qualify it separately.
Backends do not validate themselves by agreeing with another unqualified path.

Scalar fold helpers admit only sum, product, min and max. Passing the compound
`softmax_stable` operator to an otherwise matched simple-reduction path is an
invalid reduction contract: Zig returns `InvalidReductionOp`, and C returns
`TSIR_ERR_INVALID`. Neither traps nor substitutes a scalar identity. This is
distinct from `NotImplemented`/`TSIR_ERR_UNSUPPORTED` for an unsupported oracle
shape and does not claim a general softmax implementation.

## Emission

Emitters consume the resolved realization and target descriptor. They emit
residency declarations, tiled loops, PE scaffolding, collectives, and parameter
blocks. They do not choose another algorithm, infer residency, or perform hidden
semantic rescue. Necessary local conflict handling, including DSD layout,
fabric color, or memory-bank rules, is explicit and hashed into emitter identity.

A migration shim remains separately identified until its replacement passes
lowering, emission, parity, and manifest binding. Backend availability does not
inherit body-level parity from scaffold or bootstrap coverage. CSL, WebGPU,
MSL, DXIL, and SPIR-V retain separately qualified support.

## Manifest and convert boundary

`integrityExtensions.lowerings[]` binds each selected lowering to:

- kernel reference and backend;
- frontend version and target descriptor digest;
- semantic and realization digests;
- emitter/compiler identity and output digest;
- parity receipt, exactness class, and reference provenance.

Conversion may record this declared lowering through the public Program Bundle
boundary. Doppler still owns model construction and approval. Unsupported
kernels produce typed rejections; missing targets are not silently dropped.
Runtime consumes an already declared implementation rather than inventing a
replacement. Source, target, policy, or emitter changes invalidate affected
lowerings and require explicit reconstruction/requalification.

## Parity and promotion

The manual parity CLI and its receipt schema own scoped parity admission.
Reference fixtures, shape contracts, and manifest bindings are frozen per run.
A candidate crosses the complete lowering/emission/parity/binding bar before
its former path is retired. Partial compiler work stays partial.

A declared canary is advisory regression coverage. GPU parity is not made a
new PR gate by this documentation change. Historical phase-review and kernel
ordering rules remain attached to their original experiment contracts; they
cannot start a new campaign. Retain rejected candidates, typed unsupported
results, interference, and missing evidence.

The historical Gemma/Cerebras acceptance requires the selected real model's
kernels to reach CSL parity against its frozen Doppler transcript, with the
receipts bound into that manifest. Bootstrap bodies, compile success, simulator
launches, and CPU reference transcripts do not establish full-model hardware
inference or a performance advantage.

## Owners and evidence

- [Shader compiler architecture](shader-compiler-architecture.md)
- [CSL architecture](csl-architecture.md) and [Doppler ingest](doppler-ingest.md)
- [Schemas](../config/) and [TSIR source](../runtime/zig/src/compiler/tsir/)
- [Parity CLI](../bench/tools/doe_parity.py) and `reports/parity/`
- [Current TSIR status](status/tsir.md) and [Cerebras acceptance](cerebras-model-ledgers.md)
