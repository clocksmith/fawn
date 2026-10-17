# Doe technical thesis

[GOALS.md](../GOALS.md) owns product goals, constraints, and exclusions.
This document explains their technical rationale and the proofs that select
current work; it does not maintain a second goal list. [INTENT.md](../INTENT.md)
owns enforceable invariants, component charters assign authority, and artifacts
establish current results. The commercial plan and the ordered roadmap are
maintained in the owner's private strategy repository, not here.

Doe aims to become Chromium's superior GPU execution foundation. The
[acceptance hierarchy](../GOALS.md#acceptance-hierarchy) requires unchanged
WebGPU application wins, page WebGPU ownership, measured expansion into wider
GPU paths, and maintainability. Fawn provides durable browser workspaces;
DoeLab tests improvements and DoeProof records evidence.

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

Standalone workspace qualification does not inherit or require native runtime
wins; its recorded evidence retains that scope. The integrated product follows
the [technical proof sequence](../GOALS.md#technical-proof-sequence): establish
Doe-owned webpage execution before qualifying a durable workflow on that
foundation, then establish external retention. Fawn task value and Doe provider
value remain distinct gates. Chromium hooks and browser-runtime integration
still require measured benefits that justify compatibility and maintenance costs.
Doppler retains model semantics; independently qualified DoeRuntime integration
serves workflows where useful. Neither browser hooks nor runtime replacement
defines the integrated product.

Node, Bun, Doppler and ONNX supply workloads, integration surfaces and
regressions. Inference and general compute are proving lanes alongside graphics;
they do not define Doe. Doppler retains model semantics. Transfer across
applications helps distinguish an execution-layer improvement from an
application-specific accommodation. Independently useful compiler and native
execution remain valid outcomes; the central objective additionally requires
Chromium ownership and maintainability.

## Chromium foundation acceptance

`gpuFoundation` in [the strategy contract](../config/doe-product-strategy.json)
records the ordered acceptance hierarchy separately from the existing campaigns.
Page ownership requires a trace from an ordinary webpage's device creation
through shaders, resources, submission, completion, readback, presentation and
destruction, bound to the Doe runtime. A disabled-Doe negative control must
make the operation fail. Compare stock Chromium+Dawn with forced-Doe Chromium
on the same pinned source and build configuration except for the declared
integration. Application code remains unchanged and fallback is forbidden.
Fawn workspace changes must not confound this provider comparison.

The [maintainability contract](../browser/chromium/acceptance.md#maintainability-acceptance)
requires reviewable patch scope, independently reproducible builds, successful
version rebases, measured backend deduplication, build/test costs within frozen
bounds, and ownership/diagnostic evidence. Gather these during each integration;
their place last in the hierarchy does not defer maintenance until after
performance tuning. Missing bounds, failed rebases or absent evidence prevent
accepting the foundation even when correctness and performance pass.

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

Three retained campaigns select current technical work within that hierarchy,
recorded in `roadmapProofs` of
[the strategy contract](../config/doe-product-strategy.json). Correctness
repairs, independent installation and technical release are not blocked by them.
Their frozen contracts remain unchanged; passing them alone does not discharge
the full Chromium foundation hierarchy or its maintainability requirements.

1. **Current Doe against Dawn on the initial qualification target.** Re-run the
   native and package rows on current code, audit suspicious wins, then run
   complete Doppler generation. Acceptance requires correctness, stable A/A
   controls, a p50 and p95 advantage, and CPU and memory bounds. The immediate
   WebGPU graphics family below is also required; inference alone cannot
   establish this proof's application win set.
2. **Current Metal confirmation.** Reproduce the strongest rows on current code,
   audit every result at or above the suspicious-speedup ratio in
   [the methodology thresholds](../config/benchmark-methodology-thresholds.json),
   and fix or explain the ONNX Runtime tail regression. Historical artifacts in
   ignored output do not count. Begin this separate campaign after freezing the
   accepted Vulkan application checkpoint. Shared semantics remain required;
   backend-specific optimizations need not be identical or win on both backends.
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

## Graphics proof families

WebGPU application graphics is immediate Doe-versus-Dawn work under proof 1.
The selected family covers a complex Three.js WebGPU scene, a 2D image/filter
pipeline, particles or fluids, and texture upload through canvas presentation.
Keep application code, shaders, assets, inputs and rendering requirements
unchanged between providers. A Three.js WebGL renderer is a different path.

Freeze each workload's independent correctness oracle, provider and browser
identity, viewport, animation/input sequence, cache policy, sampling and winning
margins before measuring. Require complete-frame p50/p95, preparation, CPU cost,
GPU execution observations and GPU memory accounting, with process RSS and
requested allocations reported separately. Distinguish application submission,
GPU completion and displayed-frame timing; requestAnimationFrame intervals or
offscreen readback alone do not establish canvas presentation. Stable A/A
controls, failures, tails and memory bounds follow the
[performance contract](performance-strategy.md). Missing work or metrics blocks
admission. New frozen campaign contracts and executable checks must implement
these requirements before timing can establish a win.

Wider Chromium rendering is a later, separate family: animated CSS blur and
backdrop-filter, transforms and opacity, scrolling and compositing, Canvas 2D
and SVG rasterization, and video with filtered overlays. Blink, Skia, cc, Viz,
ANGLE and media paths have distinct responsibilities; GPU acceleration does not
imply that the work crosses the page WebGPU seam. Chromium's
[cc overview](https://chromium.googlesource.com/chromium/src/+/main/docs/how_cc_works.md)
describes rasterization, scrolling and frame submission. Skia
[Graphite uses Dawn](https://blog.google/chromium/introducing-skia-graphite-chromes/)
for native GPU access, including rasterization and compositing, so some browser
rendering can already reach Dawn without a page calling WebGPU. Record the
actual backend for the pinned browser instead of assuming a universal path.

After WebGPU application wins are established, select integration below or
beside Skia/Viz/ANGLE, including Graphite's Dawn backend where applicable, only
when measured opportunity justifies it. Qualify ownership, output, compatibility,
complete-frame benefit and maintenance cost separately. The Chromium component
owns selected integration; wider paths require selection before implementation.
The frozen proof-3 contract and its build gate remain unchanged; its page-WebGPU
attribution does not establish wider rendering ownership. Historical compiler screens retain
their rejected verdicts and do not qualify either new family.

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
