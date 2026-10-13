# Doe strategy: durable browser work and independent GPU execution

[GOALS.md](../GOALS.md) owns product goals, constraints, and exclusions.
This document explains their rationale and sequencing; it does not maintain a
second goal list. [INTENT.md](../INTENT.md) owns enforceable invariants, component
charters assign authority, and artifacts establish current results.

Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.

Next, build Fawn's shared observation, action, and recovery service above stock
Chromium, using narrow CDP/Playwright adapters. Persist ownership generations,
reject stale commands, and retain completed, failed, or unknown outcomes across
restart. Prove consistent behavior across research, form submission, and export
workflows; task-specific logic remains outside the shared machinery.
The [workspace contract](fawn-workspace-contract.md) defines acceptance, and
`fawnWorkspace` in the [strategy projection](../config/doe-product-strategy.json)
owns the ordered work. This service is not implemented or qualified yet.

Fawn acceptance does not wait for native runtime wins. First establish durable
authority and uncertainty handling, then prove reuse and complete-task value
against competent Chromium/Playwright automation. Add Chromium hooks and
browser-runtime integration only when measured benefits justify their
compatibility and maintenance costs. Doppler retains model semantics;
independently qualified DoeRuntime integration serves workflows where useful.
Neither browser hooks nor runtime replacement defines the integrated product.

Separately, keep DoeRuntime focused on one correct, reproducible Doppler Node
generation advantage over Dawn on Vulkan, then ONNX transfer. Local AI supplies
demanding existing workloads and repeated execution costs to improve. Doppler is
the first engineering customer and owns its models and inference. Transferring
the improvement through the existing ONNX integration establishes that it belongs
to the execution layer rather than a model-specific accommodation.
Compiler opportunities can be selected independently when they have their own
consumer and attributable outcome. Browser replacement is a later, optional
integration route, not a prerequisite for native value.

## Ordered execution milestones

The schema-validated [strategy contract](../config/doe-product-strategy.json)
owns the native/runtime milestone IDs, binding proving set, and development metric.
This preserved sequence governs optional runtime substitution, independently of
the selected Fawn workspace roadmap.

1. Close ordinary execution with one correct, reproducible Doppler Node
   generation advantage over Dawn on Vulkan. Retain accepted improvements and
   correctness repairs;
   finish one bounded, evidence-backed optimization against a frozen baseline.
   Require repeatable material advantage in a declared complete-application outcome
   with predeclared regression limits for the others. Simultaneous latency, CPU,
   and memory victory is not required. Reject ineffective candidates, preserve
   the baseline, and record a new prioritization decision instead of extending
   one reranker indefinitely. Rejection does not satisfy the advantage milestone.
2. Transfer to ONNX through independent framework integration. Confirm that the
   accepted Doppler correction carries through the pinned ONNX Runtime WebGPU
   substitution, which already executes existing operators on the qualified
   Vulkan host.
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

DoeRuntime emphasis remains one correct, reproducible Doppler Node generation
advantage over Dawn on Vulkan, then ONNX transfer; the existing ONNX evaluation
distribution is sufficient for adoption work.
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
retained adoption remain open. Chromium hooks and browser-runtime integration
stay deferred until measured benefits justify their compatibility and maintenance
costs and a separate selection decision is made. Fawn's shared workspace service
above stock Chromium is the next build, selected independently. Existing A/B/C/D
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
