# Doe technical thesis

[GOALS.md](../GOALS.md) owns product goals, constraints, and exclusions.
This document explains their technical rationale and the proofs that select
current work; it does not maintain a second goal list. [INTENT.md](../INTENT.md)
owns enforceable invariants, component charters assign authority, and artifacts
establish current results. The commercial plan and the ordered roadmap are
maintained in the owner's private strategy repository, not here.

Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.

## Independent results

Fawn's shared workspace service runs above stock Chromium with narrow
CDP/Playwright adapters, persists ownership generations, rejects stale commands,
and retains completed, failed, or unknown outcomes across restart, consistently
across the research, form-submission and export workflows; task-specific logic
stays outside the shared machinery. Three states stay distinct:
- **Behaviorally qualified**: established on the frozen case set for a recorded
  commit, browser binary and host.
- **Product-advantaged**: established only on the frozen differentiation suites;
  this is not broad product superiority.
- **Release-qualified**: not established.

The [workspace contract](fawn-workspace-contract.md) defines acceptance,
`fawnWorkspace` in the [strategy contract](../config/doe-product-strategy.json)
binds each step's assessment to admitted evidence, and
[status](status/fawn-workspace.md) records what is established.

Fawn acceptance does not wait for native runtime wins. Fawn must show why
someone would choose it, and DoeRuntime why someone would replace Dawn; neither
result depends on the other. Add Chromium hooks and browser-runtime integration
only when measured benefits justify their compatibility and maintenance costs.
Doppler retains model semantics; independently qualified DoeRuntime integration
serves workflows where useful. Neither browser hooks nor runtime replacement
defines the integrated product.

Local AI supplies demanding existing workloads and repeated execution costs to
improve. Doppler is the first engineering consumer and owns its models and
inference. Transferring an improvement through the existing ONNX integration
establishes that it belongs to the execution layer rather than a model-specific
accommodation. Compiler opportunities can be selected independently when they
have their own consumer and attributable outcome. Browser replacement is an
optional integration route, not a prerequisite for native value.

## Three proofs

The bar is not usefulness beside Dawn and Chromium. Removing Doe or Fawn and
returning to Dawn or stock Chromium must make the product measurably worse.
Current evidence shows promise, not that result:
- Doe's wins over Dawn are historical, uneven and partly microbenchmark-only,
  and the decisive current real-model run is missing.
- The extreme Metal upload rows lack fairness audits.
- ONNX Runtime on Metal shows a tail loss.
- Fawn improves agent work on Chromium; it has not beaten Chromium as an
  integrated browser.

Three proofs select current technical work, recorded in `roadmapProofs` of
[the strategy contract](../config/doe-product-strategy.json). Correctness
repairs, independent installation and technical release are not blocked by them.

1. **Current Doe against Dawn on the initial qualification target.** Re-run the
   native and package rows on current code, audit suspicious wins, then run
   complete Doppler generation. Acceptance requires correctness, stable A/A
   controls, a p50 and p95 advantage, and CPU and memory bounds.
2. **Current Metal confirmation.** Reproduce the strongest rows on current code,
   audit every result at or above the suspicious-speedup ratio in
   [the methodology thresholds](../config/benchmark-methodology-thresholds.json),
   and fix or explain the ONNX Runtime tail regression. Historical artifacts in
   ignored output do not count.
3. **Integrated Fawn and Doe against stock Chromium and Dawn.** Build the
   forced-Doe Chromium path, then run identical browser and agent workloads on
   stock Chromium with Dawn, the Fawn workspace with Dawn, and the Fawn workspace
   with Doe. [The integrated comparison contract](../config/fawn-doe-integrated-comparison.json)
   froze its workloads, oracles, controls and decision rules before any checkout
   or build. Proof 3 selects the forced-Doe Chromium build for this comparison
   only, not for adoption.

Fawn earns the larger claim only when the complete integrated system beats stock
Chromium in task success, latency, memory, recovery and demanding GPU workloads.
Until then, Doe has promising execution evidence and Fawn has scoped workspace
advantages; neither has achieved the final objective.

## Own the executable program

The proposed primitive is a reusable GPU program: shaders, dependencies,
resource requirements, permitted input variations, and executable plans.
Optimize allocations, pipeline preparation, transfers, command construction,
synchronization, and repeated submission as well as kernels. Retain useful
state and rebuild only affected assumptions.

Safe reuse is an execution mechanism subordinate to the selected proofs. Start
with declared fixed-shape computation when repeated preparation is a measured
cost; keep ordinary execution available where reuse is unsuitable. Identity,
lifetime, replacement, and proof obligations belong to [INTENT.md](../INTENT.md),
refined by the [reusable-program contract](reusable-compute-programs.md).

The WGSL compiler, native backends, resource contracts, and declarative plan
executor are foundations. Their existence does not establish a complete program
compiler. The initial additive API and its limits live in
[reusable compute programs](reusable-compute-programs.md).

## Enter through compatibility and earn reuse

Make `doe-gpu` straightforward to try in controlled Node, Bun, Electron, and
native applications. Preserve familiar WebGPU operations and existing WGSL.
Offer repeated computation through an optional declared-program interface.
Replacing a provider cannot eliminate arbitrary JavaScript orchestration that
an application continues to execute.

Each ordinary application compares the strongest eligible incumbent and ordinary
Doe. Prepared DoePlan is an optional, separately disclosed treatment. The declared
complete-operation outcome owns acceptance; kernel timing helps diagnose it. Retain cold initialization, preparation, first
execution, repeated tails, CPU and GPU work, memory, allocations, submissions,
teardown, and recovery. Missing measurements remain missing acceptance evidence.

The promise is the same useful computation with less repeated work, smaller
resource demands, and clearer failures. Compatibility substitution and explicit
program integration are different treatments: freeze and disclose each one,
and give incumbent controls equivalent persistent pipelines, bindings, batching,
and caches at the application boundary; internal implementations may differ. An
interface alone is not an ownership win if the same optimization works equally
well above an incumbent.

Prepared workflows already exist in
[CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html),
and IREE combines scheduling with execution compilation. Doe must differentiate
through WebGPU integration, deployment simplicity, safe reuse, and measured
application outcomes; graph execution is not a Doe invention.

Ordinary browser imports still use browser-owned WebGPU. Browser replacement
requires its own integration, artifact, and acceptance evidence.

Deployment and platform promises remain bounded by
[GOALS.md](../GOALS.md#strategic-constraints). Reduce installation and repeated
execution costs on named consumer paths before expanding the support matrix.

## Make corrections transferable

Preserve relationships from original WGSL through IR and backend programs to
execution. Make failures reproducible and effects inspectable. A contributor
should supply a small reproduction and comparison without understanding the
entire runtime.

```text
application failure or measured repeated work
-> minimized source and frozen independent oracle
-> general compiler transformation, resource policy, or backend correction
-> permanent regression and physical evaluation
-> transfer to an unrelated program
-> maintained release gate
```

DoeLab owns this failure-to-correction process and tests each improvement
against frozen criteria before promotion. Its next product-level test is
whether accumulated corrections make subsequent applications easier to support,
not whether the regression archive grows. Keep source, interfaces, and
reproduction tools open. Compound development momentum, maintained integrations,
and implementation knowledge through useful results.

## Demonstrate a newly practical application

Start with the retained ordinary-execution baseline on physical AMD Vulkan.
Prepared image-processing and scientific computations remain separate optional
treatments. Qualify additional hardware independently around demonstrated
advantages rather than transferring support claims.

Before tuning, freeze source, inputs, independent numerical requirements,
hardware and driver, fallback policy, lifecycle obligations, timing scopes,
memory accounting, and a meaningful application outcome. Identity hashes bind
bytes; numerical comparisons establish acceptable results.

Compare against competently configured Dawn and wgpu with persistent pipelines,
batching, and caches. Report preparation, cold startup, repeated latency and CPU
cost, peak memory, cancellation, recovery, and when preparation is recovered.
Keep raw samples, tails, failures, and structural work. Distinguish process RSS
and requested buffer bytes from peak GPU memory. Disclose host and effective
readback mechanisms and verify equivalent output/completion semantics under
the declared [comparison class](performance-strategy.md#comparison-classes).
Verified elimination of unnecessary internal work can establish a scoped advantage.

Cross a predeclared useful boundary: a missed interactive deadline, an analysis
memory limit, or unacceptable repeated CPU cost. Numerical parity alone does
not establish that crossing. A transferred correctness fix does not establish
a transferred performance breakthrough. Preserve losses and audit implausibly
large wins before accepting them.

Repository-owned applications can prove a mechanism. External voluntary
adoption and retention require separate evidence from the application's owner.
Receipts, internal benchmarks, sibling integrations, and qualification do not
establish adoption.

For a retained adoption claim, record the external owner's dependency decision,
supported scope, rollback path, and release evidence. Requalify after a
consequential application, driver, runtime, adapter, or hardware change and retain
the owner's second release decision. Initial substitution and a measured benefit
do not establish continued use.

## Ownership and independent controls

Independent adoption and impartial qualification follow
[GOALS.md](../GOALS.md#strategic-constraints) and
[INTENT.md](../INTENT.md#promotion-and-evidence-integrity). Use those boundaries
when choosing an opportunity: a compiler consumer need not replace its runtime,
and Doppler's engineering feedback does not establish external retained use.

Preserve I0, I1, W0, D0, and credible eligible P0 under the governed
[runtime ownership decision](runtime-ownership-decision.md). An unchanged
application measures provider substitution. An explicit program integration
measures the disclosed application/runtime treatment under identical useful
work. Each freezes the strongest control; neither inherits the other's result.
Doppler+Doe versus Doppler+Dawn attributes the provider; ordinary Chromium plus a
pinned native engine is a separate complete-system alternative that cannot
isolate Doe's compiler or runtime.

Priorities remain correctness, operational reliability, compatibility, material
end-to-end value, simple installation, and useful diagnostics and replay.
Receipts cannot compensate for incorrect, unstable, incompatible, or slower work.

## Engineering discipline

Execute selected work under the [performance contract](performance-strategy.md)
and [system invariants](../INTENT.md). The ordinary, qualification, and offline
improvement loops remain distinct: execute a selected version; independently
evaluate it against frozen criteria; then promote or reject a bounded candidate.
Ordinary applications do not have to run the latter two loops.

Measure reproduction-to-verified-improvement time from the retained reproducible
problem to independent confirmation under frozen acceptance
(`developmentEffectiveness` in the [strategy contract](../config/doe-product-strategy.json)).
Link timestamps, source, and evidence in existing work records; keep rejected and
unresolved attempts visible without treating them as verified improvements.

Keep compiler, backends, and host adapters independently testable. Reuse,
refactoring, and agent assistance serve the selected work. Give each semantic
decision one owner, make resource acquisition and release complete, and remove
unnecessary allocation, repeated analysis, and command preparation.
Qualification verifies these improvements. Use compile-time programming for
facts known at build time; resolve physical device and driver facts at
initialization when their lifetime permits. Shared algorithms must reduce real
work without hiding backend-specific responsibilities. Preserve characterizing
tests and check execution cost, generated code size, and build cost before
accepting a structural optimization.

Compiler/runtime ownership covers transformations, reusable plans, invalidation,
resource lifecycle, and backend execution. Application/evaluation ownership
covers integration, packaging, independent controls, diagnostics, and reproducible
tests. Both own the frozen application outcome and transfer test.

Keep demonstrations inside [the explicit exclusions](../GOALS.md#explicit-exclusions).
Dynamic shapes require a separately qualified extension of the fixed-shape
contract. Accelerator work retains separate hardware admission and cannot
broaden the active matrix through simulator evidence.

## Technical policy owners

- [GOALS.md](../GOALS.md) owns the product goals, constraints, exclusions, and
  the backend focus.
- [INTENT.md](../INTENT.md) owns enforceable system invariants.
- [CATSCAN.md](../CATSCAN.md) and child charters own component authority.
- [Reusable compute programs](reusable-compute-programs.md) owns initial API,
  invalidation, reproduction, and limitations.
- [Developer wedge](node-bun-developer-wedge.md) owns package integration and
  downstream promotion.
- [Strategy contract migrations](product-strategy-contract.md) document
  `config/doe-product-strategy.json`.
- [Performance](performance-strategy.md), [workloads](workload-system.md), and
  [process](process.md) own measurement, evidence, and stage law.
- [Ecosystem](ecosystem.md), `config/ecosystem-registry.json`, and
  `reports/ecosystem/` own external evaluation and adoption state.
- [Support matrix](doe-support-matrix.md) and `reports/claim-index.json` own
  promoted support and public claim eligibility.
- [Browser lane](browser-lane.md) owns the separate browser evidence path.

Artifacts own current outcomes. Rationale prose does not promote an
implementation, benchmark, package, browser, or adoption claim.
