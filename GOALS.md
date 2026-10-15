# Doe goals

## Mission and thesis

**Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.**

Fawn begins above stock Chromium through one shared observation, action, and
recovery service with narrow CDP/Playwright adapters. DoeRuntime and the
independently usable WGSL compiler keep GPU programs executable, inspectable, and
portable across hardware; neither their adoption nor Fawn requires the other.
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

The independent compiler and runtime retain these outcomes:

1. **Faster unchanged applications.** Replace a supported application's WebGPU
   provider without changing its shaders, models, inputs, application logic, or
   declared numerical requirements. Remove unnecessary preparation, transfers,
   synchronization, and memory overhead; earn the result in complete operations.
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
   validation only when they remain established. Use Zig for structural checks
   and Lean for selected obligations; retain validation when changed inputs or
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
   boundaries readable and independently testable. Reduce duplication and
   measure build cost and reproduction-to-verified-improvement time. Zig enables
   engineering discipline; choosing it is not itself a product advantage.

## Strategic constraints

- Backend focus: **Vulkan on the initial qualification target, then a current Metal confirmation, both selected as proofs; D3D12 deferred.**
  Work proceeds in bounded batches. Existing regression checks do not authorize
  parallel optimization campaigns or silently expand that boundary.
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
  Canvas 2D, video composition, rasterization and page compositing need separate
  integration and measured benefit on their actual execution paths. Replacing
  page WebGPU does not establish those benefits. Collaborating products receive
  no private provider preference.
- Adoption: supporting-feature value, receipts, internal benchmarks, and sibling
  integrations do not establish independent adoption or retained use.
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
