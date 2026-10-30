# Doe goals

## Mission and thesis

**Fawn is the whole project and the brand of its Chromium fork. Doe is the WebGPU
implementation that powers Fawn. Doe's central objective is to become Chromium's
superior GPU execution foundation; compute is one proving lane alongside graphics.
DoeLab tests improvements; DoeProof records evidence.**

Doe also runs outside the browser, through Node, Bun and Deno and through native
embedding, with the independently usable WGSL compiler keeping GPU programs
executable, inspectable and portable across hardware. Standalone Doe use does not
require the Fawn browser. Durable browser workspaces are an optional Fawn feature:
one shared observation, action and recovery service with narrow CDP/Playwright
adapters, which can also run above stock Chromium.
This file owns the product goals, constraints, and exclusions. These are desired
outcomes, not claims of qualified support or completed adoption.
[The thesis](docs/thesis.md) owns the technical rationale and the proofs that
select current work; the commercial plan and ordered roadmap are maintained in
the owner's private strategy repository.

**Final objective:** removing Doe or Fawn and returning to Dawn or stock Chromium
makes the product measurably worse. Usefulness beside Dawn and Chromium does not
meet this bar. Doe has promising execution evidence and Fawn has scoped workspace
advantages; neither has achieved the objective. [Three proofs](docs/thesis.md#three-proofs)
select the current work.

## Technical proof sequence

Vulkan application win → frozen Vulkan checkpoint → separate Metal confirmation
campaign → Doe-owned webpage execution in Fawn → durable integrated Fawn workflow
→ external retention. Engineering simplification is a continuous constraint on
every campaign, not a later phase. Doe must become easier to improve while
simultaneously becoming harder to replace.

Fawn is the controlled Chromium proving environment; GPU ownership and useful
durable work have distinct gates. Existing standalone workspace qualification
remains valid in its recorded scope. It neither qualifies Doe's webpage execution
nor skips the integrated sequence. External retention is the final adoption gate,
not a prerequisite for technical qualification. This sequence does not relabel
historical results or relax frozen campaign criteria.

## Acceptance hierarchy

1. **Beat Dawn for unchanged WebGPU applications.** Qualify inference and
   general compute, a Three.js scene, an image/filter pipeline, particles or
   fluids, and texture upload through actual canvas presentation. Require
   p50/p95 advantage, preparation, CPU, GPU memory, independent correctness and
   no fallback under frozen workload contracts.
2. **Own page WebGPU inside Chromium.** Real webpage device creation through
   destruction must be Doe-owned. Compare stock Chromium+Dawn with forced-Doe
   Chromium using ordinary applications unchanged, including compute and
   presentation, callbacks and futures, cancellation, device loss, resource
   settlement, browser shutdown, and security behavior. Keep forced provider
   selection observable, reject fallback, and demonstrate explicit rollback to
   Dawn. Every object remains bound to its creating provider; existing Dawn
   objects must never silently become Doe objects. Native benchmarks alone cannot
   establish a dependable working browser replacement. Doe owns GPU execution
   and diagnostics, not browser UI or agent policy.
3. **Expand into Chromium's wider GPU paths.** Trace the real execution path
   before selecting Graphite/Skia rasterization and compositing, CSS filters and
   backdrop filters, Canvas 2D, SVG, scrolling or video overlays. Each integration
   must prove its own benefit after the preceding WebGPU obligations.
4. **Prove maintainability.** Require a narrow Chromium patch set, reproducible
   builds, successful Chromium-version rebases, reduced duplicated backend code,
   bounded build/test cost, and readable ownership and diagnostics. Collect this
   evidence throughout integration; performance and correctness alone cannot
   establish the final objective. The
   [Chromium acceptance contract](browser/chromium/acceptance.md#maintainability-acceptance)
   defines the required evidence and failure conditions.

Node, Bun, Doppler and ONNX remain workload generators, integration surfaces and
regressions. They do not define the product. Independent native/compiler use
remains supported; it does not satisfy Chromium ownership or maintainability.
The existing three proofs are campaigns within this hierarchy, not substitutes
for its acceptance. Fawn workspace acceptance remains separate.

## Desired outcomes

Fawn provides one reusable workspace contract spanning observation, action,
ownership and recovery. The service persists ownership generations, rejects stale
commands, and retains completed, failed, or unknown outcomes across restart.
Research, form-submission, and export workflow adapters share this machinery and
must behave consistently. Durable authority and honest uncertainty precede
product-value claims. Chromium hooks and browser-runtime integration are added
only when measured benefits justify their compatibility and maintenance costs.
[The workspace contract](docs/fawn-workspace-contract.md) owns its
requirements; `fawnWorkspace` in the [strategy contract](config/doe-product-strategy.json)
binds its acceptance steps to admitted evidence. These are intended outcomes, not
implemented service claims.

After Doe-owned webpage execution passes its gate, the integrated product target
is one useful durable workflow with ownership and task state preserved across
interruption. Display uncertain and failed outcomes honestly. Require a material
complete-task advantage over competent stock Chromium automation on a frozen user
task, then establish an external user who keeps the replacement enabled. Fixture
qualification and internal use do not establish retained external adoption.

The independent compiler and runtime retain these outcomes:

1. **Faster unchanged applications.** Replace a supported application's WebGPU
   provider without changing its shaders, models, inputs, application logic, or
   declared numerical requirements. Remove unnecessary preparation, transfers,
   synchronization, and memory overhead; earn the result in complete operations.
   Finish one Vulkan application win against a source-matched Dawn control,
   preserving the application, shaders, outputs, numerical requirements,
   cancellation, and cleanup. Accept a material benefit in complete application
   latency, memory use, CPU cost, or reliability; an internal counter improvement
   alone is insufficient. Reverting the provider must make the unchanged
   application materially worse. Freeze the accepted Vulkan checkpoint, then
   conduct Metal confirmation as a separate campaign. The same optimization need
   not win on both: shared compiler semantics transfer where valid, while backend
   resource and synchronization improvements may differ. This first win does not
   waive frozen comparison criteria or the broader acceptance hierarchy.
   WebGPU application graphics belongs in the required win set alongside
   compute: scenes, games, CAD, maps, particles, fluids, image and video effects,
   charts, simulations, canvas presentation, and GPU-driven interfaces when
   their work executes through WebGPU. Qualify each selected workload; this is
   an intended outcome, not a claim that every application is supported or faster.
2. **Reusable GPU programs.** Describe repeated computation once and retain
   compatible pipelines and resources. Keep descriptions, prepared resources,
   and invocations distinct; failed replacement preserves the working program.
   Reuse is optional, with fixed-shape limits explicit.
3. **An independently useful compiler.** Make WGSL validation, diagnostics,
   optimization, and target emission useful independently of runtime adoption.
   Measure compilation cost separately from execution and attribute compiler
   and runtime improvements separately.
4. **Prove away repeated checks.** Enforce preconditions and remove repeated
   validation only when they remain established. Use structural checks
   and selected proof obligations; retain validation when changed inputs or
   resources invalidate assumptions.
5. **Improvement that compounds.** Turn minimized reproductions into general
   corrections, independent verification, and transferable regressions. Agents
   propose changes offline; production runs selected versions. Keep promotion
   reversible and retain rejected experiments.
6. **Safe, dependable execution.** Make budgets, initialization, ownership,
   completion, ordering, and failure behavior explicit. Preserve device and
   concurrent-work isolation. Cancellation is not completion, and unresolved
   GPU work cannot authorize resource reuse or destruction.
7. **Practical independent adoption.** Provide native and JavaScript integration,
   predictable installation, and qualified support. Compiler and runtime adoption
   are independent; neither Doppler nor Reploid is mandatory. Browser replacement
   requires measured benefits, an explicitly selected integration, and its own
   evidence.
8. **Exceptional implementation quality.** Keep compiler, object, and backend
   boundaries readable and independently testable. Consolidate duplicated code
   and contradictory guidance, remove proven dead paths, and shorten targeted
   verification without weakening safety coverage. Every campaign must improve
   or preserve engineering friction under the
   [evaluation rules](docs/process.md#language-and-cleanup-evaluation), including
   owner discovery, decisive tests, clean/incremental builds, agent iteration,
   inspection burden, rebasing, and regression escape rate. Keep one authoritative
   rule source; test reduction must preserve semantic, ownership, callback,
   recovery, and physical-execution defect detection.
   The maintainability acceptance above is mandatory for the Chromium foundation.
   The selected C migration must earn engineering and product gains; language
   choice alone establishes neither.

## Strategic constraints

- Backend focus: bounded correctness, quality and matched-performance qualification
  across Vulkan, Metal and D3D12. Each physical backend keeps its own evidence;
  shared source and cross-machine synchronization do not transfer qualification.
  The initial Vulkan application proof and integrated browser gates remain open.
- Modern-stack scope is an explicit supported OS/GPU/driver/API envelope in the
  [support matrix](docs/doe-support-matrix.md), not a promise to cover historical
  platforms. Prefer simpler owners and fewer compatibility paths within that
  envelope; preserve declared behavior, independent oracles and explicit rejection
  outside it. Never narrow the envelope after seeing a failed comparison.
- Chromium change tracking and bounded repair are core engineering capabilities
  for Fawn's browser implementation and Doe's GPU integration. Their
  [acceptance](browser/chromium/acceptance.md#upstream-tracking-and-repair)
  requires unchanged qualification, rollback and measured repair reliability;
  a generated proposal is not an accepted repair.
- Initial deployment: hardware-first local execution on one frozen workstation
  (AMD Ryzen AI Max+ 395, Radeon 8060S, RADV), labelled the
  [initial qualification target](config/doe-product-strategy.json) until the
  applicable acceptance passes. It is an engineering target, not a qualified
  configuration, a mandatory dependency, or a universal performance claim.
  Fawn's goal stays durable browser workspaces; Doe enters only through explicit
  integration with independent qualification.
- Supported scope: qualify a bounded matrix of real workloads, runtimes,
  platforms, adapters, drivers, and backends. Each additional tuple earns support
  separately; iOS and Android require implemented paths and physical tests.
- Semantics: preserve supported WebGPU/WGSL behavior and frozen, independently
  evaluated numerical requirements. Exact outputs are required where justified;
  universal bit identity is not a product promise.
- Entry points: ordinary WebGPU through `doe-gpu` or native embedding remains
  useful without reusable programs, DoeProof, development agents, detailed
  tracing, or artifact publication. The optional fixed-shape interface and its
  extension limits belong in [reusable compute programs](docs/reusable-compute-programs.md).
- Hardware and application impartiality: use the same compiler/runtime across
  applications and vendors. Establish wins in both compute and WebGPU application
  graphics before expanding into wider browser rendering. CSS, text, SVG,
  Canvas 2D, WebGL-on-WebGPU, video composition, rasterization and page compositing
  need separate integration and measured benefit on their actual execution paths. Replacing
  page WebGPU does not establish those benefits. Collaborating products receive
  no private provider preference.
- Adoption: supporting-feature value, receipts, internal benchmarks, and sibling
  integrations do not establish independent adoption or retained use.
- Selected implementation direction: complete the incremental Doe-to-C migration
  under the [recorded decision](README.md#selected-direction-a-better-doe-in-c)
  and [owner gates](docs/c-migration/MIGRATION.md). C is selected, not awaiting a
  further language contest. Production defaults remain retained Zig until the
  relevant behavior, lifetime, consumer and backend gates pass. Preserve historical
  findings, source licenses and tests; publication is not production promotion.
  The [50% source-reduction target](docs/c-migration/QUALITY_TARGET.md) includes
  the complete delivery burden and never excuses feature or quality loss.
- Separate dependency removal, engineering improvement and product improvement.
  Measure unfamiliar maintenance cost and actual integration/application gains;
  neither a partial compiler, a cloud-only pass nor fewer lines completes the goal.
  The [prior language assessment](docs/implementation-language-assessment.md)
  remains historical evidence, not current work selection. Python browser scripting
  remains [documentation-only incubation](docs/python-browser-scripting-incubation.md);
  wider rendering still requires the existing WebGPU acceptance gates.
- Deployment: no mandatory browser or cloud service. Platform drivers and
  frameworks remain dependencies; there is no universal zero-dependency or
  zero-allocation promise. Provider substitution must permit safe reversion.
- Diagnostics: report the best established source, validation, resource,
  submission, or native failure location. Never invent a shader location for an
  unattributed hardware fault.
- Work selection: [the three proofs](docs/thesis.md#three-proofs) select current
  technical work: current Doe against Dawn on the initial qualification target, a
  current Metal confirmation, and integrated Fawn and Doe against stock Chromium
  and Dawn. Behaviorally qualified, product-advantaged and release-qualified stay
  distinct; [Fawn's status](docs/status/fawn-workspace.md) records which holds and
  where. Provider attribution (Doppler+Doe versus Doppler+Dawn) and complete-system
  alternatives (ordinary Chromium plus a native inference engine) are separate
  comparisons. New compiler opportunities require explicit selection, closed
  browser optimizer experiments remain closed, and selection never blocks
  correctness repairs, independent installation, or technical release.

## Explicit exclusions

Fawn owns its integrated workspace interface and controlled automation. Shared
workspace machinery excludes workflow-specific planning and model semantics;
the compiler/runtime exclude browser policy. Doe does not build Flutter engine
replacements,
peer-to-peer compute protocols, distributed cloud training or multi-node
scheduling, or unrestricted application/agent orchestration outside the Fawn
workspace contract. Universal
WebGPU compatibility, universal Dawn replacement, and universal accelerator
support are not promises; any expanded surface needs its own admission evidence.

## Policy owners

- [INTENT.md](INTENT.md): enforceable system invariants.
- [CATSCAN.md](CATSCAN.md): component ownership and acceptance boundaries.
- [docs/thesis.md](docs/thesis.md): technical rationale and the current proofs.
- [AGENTS.md](AGENTS.md): contributor instructions and canonical-policy discovery.
- [Documentation index](docs/INDEX.md): architecture, installation, contracts,
  status, and retained evidence.
