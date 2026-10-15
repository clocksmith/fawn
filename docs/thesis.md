# Doe strategy: durable browser work and independent GPU execution

[GOALS.md](../GOALS.md) owns product goals, constraints, and exclusions.
This document explains their rationale and sequencing; it does not maintain a
second goal list. [INTENT.md](../INTENT.md) owns enforceable invariants, component
charters assign authority, and artifacts establish current results.

Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.

Fawn's shared workspace service has passed scoped behavioral qualification and
has shown material complete-task value over competent Chromium/Playwright agent
tooling on a frozen agent-workflow suite. Its next work is the integrated
comparison in [the three proofs](#three-proofs). The service runs above stock Chromium with narrow CDP/Playwright
adapters, persists ownership generations, rejects stale commands, and retains
completed, failed, or unknown outcomes across restart, consistently across the
research, form-submission and export workflows; task-specific logic stays outside
the shared machinery.

Three states stay distinct:
- **Behaviorally qualified**: established on the frozen case set for a recorded
  commit, browser binary and host.
- **Product-advantaged**: established only on the frozen differentiation
  suites. The product message: Fawn preserves trustworthy browser-work outcomes
  across agent interruption and gives agents efficient, scoped views of changing
  applications.
  - On two applications, against Playwright agent tooling (including tooling that
    revalidates every decision and cancels pending calls on takeover), the
    workspace's recorded outcomes let an interrupted agent resume without human
    review or duplicates.
  - Its region-scoped views carry far fewer observation bytes on changing pages.
  - Stale-decision rejection and safe human handoff are built-in guarantees, not
    general advantages: a well-engineered control reproduced both with more
    orchestration.

  Scripted workflows show no material advantage over competent stock
  automation, and this is not broad product superiority.
- **Release-qualified**: not established.

The [workspace contract](fawn-workspace-contract.md) defines acceptance,
`fawnWorkspace` in the [strategy projection](../config/doe-product-strategy.json)
owns the ordered work, and [status](status/fawn-workspace.md) records the admitted
evidence.

Fawn acceptance does not wait for native runtime wins. Durable authority and
uncertainty handling came first, then scoped complete-task value against
competent Chromium/Playwright agent tooling; the integrated comparison is next.
Fawn must show why someone would choose
it, and DoeRuntime why someone would replace Dawn; neither result depends on the
other. Add Chromium hooks and
browser-runtime integration only when measured benefits justify their
compatibility and maintenance costs. Doppler retains model semantics;
independently qualified DoeRuntime integration serves workflows where useful.
Neither browser hooks nor runtime replacement defines the integrated product.

Separately, DoeRuntime's work is proofs 1 and 2: current Doe against Dawn on the
initial qualification target, ending in one correct, reproducible Doppler Node
generation advantage, then a current Metal confirmation. Local AI supplies
demanding existing workloads and repeated execution costs to improve. Doppler is
the first engineering customer and owns its models and inference. Transferring
the improvement through the existing ONNX integration establishes that it belongs
to the execution layer rather than a model-specific accommodation.
Compiler opportunities can be selected independently when they have their own
consumer and attributable outcome. Browser replacement is a later, optional
integration route, not a prerequisite for native value.

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

The roadmap narrows to three proofs, recorded in `roadmapProofs` of
[the strategy contract](../config/doe-product-strategy.json):

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
   or build.

Fawn earns the larger claim only when the complete integrated system beats stock
Chromium in task success, latency, memory, recovery and demanding GPU workloads.
Until then, Doe has promising execution evidence and Fawn has scoped workspace
advantages; neither has achieved the final objective.

## Ordered execution milestones

The schema-validated [strategy contract](../config/doe-product-strategy.json)
owns the native/runtime milestone IDs, binding proving set, and development metric.
The [three proofs](#three-proofs) select current work: milestone 1 completes
within proof 1, milestones 2 and 3 wait until the proofs resolve, and proof 3
selects the forced-Doe Chromium build for its comparison only, ahead of
milestone 4's adoption decision.

1. Close ordinary execution with one correct, reproducible Doppler Node
   generation advantage over Dawn on Vulkan. Retain accepted improvements and
   correctness repairs;
   finish one bounded, evidence-backed optimization against a frozen baseline.
   Require repeatable material advantage in a declared complete-application outcome
   with predeclared regression limits for the others. Simultaneous latency, CPU,
   and memory victory is not required. Reject ineffective candidates, preserve
   the baseline, and record a new prioritization decision instead of extending
   one reranker indefinitely. Rejection does not satisfy the advantage milestone.
2. Transfer to ONNX through independent framework integration. Once milestone 1
   accepts a Doppler correction, confirm that it carries through the pinned ONNX
   Runtime WebGPU substitution, which already executes existing operators on the
   qualified Vulkan host. No such correction is accepted yet.
   The [campaign](onnx-vulkan-campaign-contract.md),
   [lifecycle contract](native-device-lifecycle-contract.md), and
   [evaluation installation](onnx-vulkan-installation.md) establish bounded
   execution and local isolated installation. The matched source-built comparison
   rejected material advantage. Broader compatibility, external reproduction,
   material replacement value, and retained adoption remain open. Distribute the
   versioned package for another operator, document its compatibility and failure
   boundaries, and select further engineering from one real consumer requirement.
   Keep the [separate plugin EP](../runtime/bridge/onnxruntime-ep/README.md)
   as a historical experiment; do not repeat the initial feasibility investigation.
3. Make transfer binding. Exercise inference, general computation, and interactive
   rendering through unchanged applications, independent correctness references,
   and complete-operation measurements. Another framework must earn independent
   adoption; Doppler diagnostics cannot establish it. General computation and
   rendering test generality without hidden model-specific behavior. Expand
   hardware around demonstrated advantages, with separate physical qualification.
4. Optionally select bounded Chromium integration after an explicit prioritization
   decision, independent native wins, transfer evidence, and measured benefits
   that justify its compatibility and maintenance costs. Preserve ordinary
   WebGPU behavior through validation and process boundaries. Browser adoption requires
   compatibility, hostile-input security, graphics correctness, device recovery,
   maintainability, and application benefits. Existing browser evidence remains
   separately admitted; this ordering promotes no artifacts.

These are ordered acceptance milestones, not a requirement to delay technical
release or correctness work until a performance win or customer payment. The
current engineering customer is Doppler. Qualify unchanged Node generation on
current Vulkan, diagnose dominant complete-generation costs, and select one general
compiler or runtime improvement against frozen acceptance. A competent Dawn
control and an independent numerical reference provide technical evidence;
external recruitment is not an engineering gate. ONNX transfer follows the
accepted advantage; transfer evidence and voluntary adoption remain separate.
Supported embedding, maintained binaries, and support are commercial hypotheses;
validate repeated voluntary use before expanding paid offerings.

Execute each milestone under the [performance contract](performance-strategy.md)
and [system invariants](../INTENT.md). The ordinary, qualification, and offline
improvement loops remain distinct: execute a selected version; independently
evaluate it against frozen criteria; then promote or reject a bounded candidate.
Ordinary applications do not have to run the latter two loops.

## Own the executable program

The proposed primitive is a reusable GPU program: shaders, dependencies,
resource requirements, permitted input variations, and executable plans.
Optimize allocations, pipeline preparation, transfers, command construction,
synchronization, and repeated submission as well as kernels. Retain useful
state and rebuild only affected assumptions.

Safe reuse is an execution mechanism subordinate to the milestones. Start with
declared fixed-shape computation when repeated preparation is a measured cost;
keep ordinary execution available where reuse is unsuitable. Identity, lifetime,
replacement, and proof obligations belong to [INTENT.md](../INTENT.md), refined
by the [reusable-program contract](reusable-compute-programs.md).

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
Follow the ordered framework and proving-set milestones above. Prepared
image-processing and scientific computations remain separate optional treatments.
Qualify additional hardware independently around demonstrated advantages rather
than transferring support claims.

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
Receipts, internal benchmarks, sibling integrations, and paid qualification do
not establish adoption. Revenue is not required for the first proof.

For a retained adoption claim, record the external owner's dependency decision,
supported scope, rollback path, and release evidence. Requalify after a
consequential application, driver, runtime, adapter, or hardware change and retain
the owner's second release decision. Initial substitution and a measured benefit
do not establish continued use.

## Ownership and independent controls

Independent adoption and impartial qualification follow
[GOALS.md](../GOALS.md#strategic-constraints) and
[INTENT.md](../INTENT.md#promotion-and-evidence-integrity). Use those boundaries
when choosing an opportunity: a compiler customer need not replace its runtime,
and Doppler's engineering feedback does not establish external retained use.

Preserve I0, I1, W0, D0, and credible eligible P0 under the governed
[runtime ownership decision](runtime-ownership-decision.md). An unchanged
application measures provider substitution. An explicit program integration
measures the disclosed application/runtime treatment under identical useful
work. Each freezes the strongest control; neither inherits the other's result.

Priorities remain correctness, operational reliability, compatibility, material
end-to-end value, simple installation, and useful diagnostics and replay.
Receipts cannot compensate for incorrect, unstable, incompatible, or slower work.

## Focus and founder responsibilities

Select bounded work under the [backend-focus constraint](../GOALS.md#strategic-constraints).
A consumer requirement informs the next selection; it cannot silently override
that boundary or authorize a parallel campaign.

DoeRuntime emphasis is proofs 1 and 2: one correct, reproducible Doppler Node
generation advantage over Dawn on Vulkan from current code, then a current Metal
confirmation; ONNX transfer waits, and the existing ONNX evaluation distribution
is sufficient for adoption work. The first engineering target is
one frozen Strix Halo workstation, the initial qualification target
([host record](../reports/benchmarks/amd-vulkan/20261014-initial-qualification-target/README.md)).
Its qualified Node baseline comes before any advantage claim. Hardware-first
local execution is Fawn's initial deployment strategy, not a dependency of the
workspace. Doppler+Doe versus Doppler+Dawn attributes the provider; ordinary
Chromium plus a pinned native engine is a separate complete-system alternative
that cannot isolate Doe's compiler or runtime.
Measure reproduction-to-verified-improvement time from the retained reproducible problem
to independent confirmation under frozen acceptance. Link timestamps, source,
and evidence in existing work records; keep rejected and unresolved attempts
visible without treating them as verified improvements. No additional measurement
framework is needed. Keep compiler, backends, and host adapters independently
testable. Reuse, refactoring, and agent assistance serve the milestones. Give
each semantic decision one owner, make resource acquisition and release
complete, and remove unnecessary allocation, repeated analysis, and command
preparation. Qualification verifies these improvements. Use compile-time
programming for facts known at build time; resolve physical device and driver
facts at initialization when their lifetime permits. Shared algorithms must
reduce real work without hiding backend-specific responsibilities. Preserve
characterizing tests and check execution cost, generated code size, and build
cost before accepting a structural optimization.

Compiler/runtime ownership covers transformations, reusable plans, invalidation,
resource lifecycle, and backend execution. Application/evaluation ownership
covers integration, packaging, independent controls, diagnostics, and reproducible
tests. Both own the frozen application outcome and transfer test.

Keep the first demonstration inside [the explicit exclusions](../GOALS.md#explicit-exclusions).
Dynamic shapes require a separately qualified extension of the fixed-shape contract.

The ONNX Vulkan evaluation package remains the native installation front door and
the transfer target after the Doppler advantage. Independent reproduction and
retained adoption remain open. Proof 3 is the selection decision for building
the forced-Doe Chromium path; adopting Chromium hooks or browser-runtime
integration still waits for its measured benefits to justify their compatibility
and maintenance costs. Fawn's next work is that integrated comparison; broader
workloads, compatibility and deployment qualification wait. Existing A/B/C/D
and K0 browser laws retain their meaning. Accelerator work retains separate hardware
admission and cannot broaden the active matrix through simulator evidence.

## Application and partner contexts

These are target outcomes, not completed support, release commitments, or existing
customers. These contexts do not override the ordered milestones above.
Explicit reuse and agent-assisted demonstrations are supporting work. AMD and
Qualcomm are prospective technical users only. Each additional hardware/operating-system tuple requires
its own qualification.

### Independent developer: improve an application without rewriting it

Replace a Node application's current WebGPU provider with pinned `doe-gpu`,
retaining WGSL, inputs, tests, and application logic. Compare complete operation
latency, startup, CPU overhead, memory, and slow executions without weakening
validation or changing accepted results. Unsupported operations produce useful
errors; rollback is immediate. Reuse in a subsequent application establishes
repeat value. Ordinary execution must earn this result before a special
interface or application-specific optimization is required.

### Small application team: keep editing and simulation interactive

An Electron editing or simulation application prepares repeated computation,
retains compatible state, and updates parameters during use. Check shader edits
before activation; incompatible state changes require explicit approval, and
failed edits preserve the working program. Measure preview responsiveness,
bounded memory, cancellation, reopening, preparation, and final cleanup during
prolonged use. Transfer corrections into the shared runtime, not a product fork.
Electron renderer and main-process evidence remain separate.

### Research laboratory: accelerate unfamiliar numerical work

A researcher supplies a slow routine, representative inputs, and frozen
independent reference tests. An agent proposes WGSL and evaluates candidates
using Doe diagnostics under explicit process and memory budgets. Retain the
accepted program, supported-device record, and a reproducible collaborator
package. Numerical requirements never change to admit a candidate. New
algorithms and changed drivers require fresh checks; this is bounded application
development, not a general agent framework.

### AI framework maintainer: improve execution beneath applications

Evaluate the packaged pinned integration beneath ONNX Runtime’s existing WebGPU
provider. Extend its qualified scope from a concrete consumer requirement. Preserve
model interfaces and supported semantics; include loading, small dispatches,
concurrency, and output transfers
in inference comparisons across models. Framework distribution must demonstrate
dependable upgrades and explicit unsupported operations. Doe does not acquire
conversion, tokenization, or application-policy ownership.
[ONNX Runtime's provider interface](https://onnxruntime.ai/docs/execution-providers/)
is an integration reference, not evidence of a Doe provider.

### Equipment manufacturer: ship dependable offline inspection

Embed the pinned runtime in an inspection workstation with fixed camera and
measurement workloads, independent numerical checks, resource budgets, and a
qualified hardware matrix. Installation requires no technician-managed shader
toolchain or cloud image transfer. Include camera transfer and synchronization,
processing deadlines, prolonged memory behavior, fault reports, and recoverable
updates. Reuse the integration in another product. Machinery control and safety
remain separately designed and validated application responsibilities.

### AMD: improve real applications on Radeon

A prospective compiler or developer-tools evaluation runs unchanged application
corpora through Doe and strong incumbent implementations, inspects generated
programs and submissions, and contributes transferable compiler/runtime fixes.
Require reproducible complete-application benefit across workloads and separately
qualified GPU generations. Driver workarounds remain precisely scoped and
removable; vendor-specific work stays beneath independently usable interfaces.
[AMD's graphics developer resources](https://www.amd.com/en/developer/browse-by-product-type/graphics-resources.html)
provide technical context, not evidence of a relationship or accepted result.

### Qualcomm: sustain GPU features within power limits

A prospective platform/application integration targets Adreno through a
separately qualified Vulkan or D3D12 configuration. Exercise camera processing,
effects, and inference over prolonged sessions; measure energy per accepted
result, temperature, responsiveness, and memory against the strongest practical
alternative. Portable WGSL remains above architecture-specific optimization.
ARM packaging and each operating system/device integration require work.
GPU capability does not imply Hexagon NPU support.
[Qualcomm's Vulkan memory discussion](https://www.qualcomm.com/developer/blog/2026/05/high-performance-memory-extension-optimize-memory)
is technical context, not Doe qualification.

## Strategy execution map

- [GOALS.md](../GOALS.md) owns the product goals, constraints, and exclusions.
- [INTENT.md](../INTENT.md) owns enforceable system invariants.
- [CATSCAN.md](../CATSCAN.md) and child charters own component authority.
- [Reusable compute programs](reusable-compute-programs.md) owns initial API,
  invalidation, reproduction, and limitations.
- [Developer wedge](node-bun-developer-wedge.md) owns package integration and
  downstream promotion.
- [Product strategy contract](product-strategy-contract.md) maps strategy into
  `config/doe-product-strategy.json`.
- [Performance](performance-strategy.md), [workloads](workload-system.md), and
  [process](process.md) own measurement, evidence, and stage law.
- [Ecosystem](ecosystem.md), `config/ecosystem-registry.json`, and
  `reports/ecosystem/` own external evaluation and adoption state.
- [Support matrix](doe-support-matrix.md) and `reports/claim-index.json` own
  promoted support and public claim eligibility.
- [Browser lane](browser-lane.md) owns the separate browser evidence path.

Artifacts own current outcomes. Strategy prose does not promote an
implementation, benchmark, package, browser, or adoption claim.
