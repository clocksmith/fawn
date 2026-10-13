# Doe goals

## Mission and thesis

**Build Fawn as the integrated environment where humans and agents perform
durable browser work. GPU programs remain independently useful software:
executable, inspectable, and portable across hardware.**

Fawn begins above stock Chromium through a trusted workspace service and narrow
CDP/Playwright adapters. Doe also develops an independently usable WGSL compiler
and native WebGPU runtime; neither their adoption nor Fawn requires the other.
This file owns the product goals, constraints, and exclusions. These are desired
outcomes, not claims of qualified support or completed adoption.
[The thesis](docs/thesis.md) owns the rationale and execution sequence.

## Desired outcomes

Fawn provides one reusable workspace contract spanning observation, action,
ownership and recovery. Workflow adapters share this machinery. Durable authority
and honest uncertainty precede product-value claims; browser modifications require
demonstrated authority, visibility or efficiency unavailable through existing
interfaces. [The workspace contract](docs/fawn-workspace-contract.md) owns its
requirements; the [strategy](config/doe-product-strategy.json) owns its ordered
acceptance steps. These are intended outcomes, not implemented service claims.

The independent compiler and runtime retain these outcomes:

1. **Faster unchanged applications.** Replace a supported application's WebGPU
   provider without changing its shaders, models, inputs, application logic, or
   declared numerical requirements. Remove unnecessary preparation, transfers,
   synchronization, and memory overhead; earn the result in complete operations.
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
   requires an explicitly selected integration and its own evidence.
8. **Exceptional implementation quality.** Keep compiler, object, and backend
   boundaries readable and independently testable. Reduce duplication and
   measure build cost and reproduction-to-verified-improvement time. Zig enables
   engineering discipline; choosing it is not itself a product advantage.

## Strategic constraints

- Backend focus: **Vulkan now; Metal after explicit prioritization and handoff; D3D12 deferred.**
  Work proceeds in bounded batches. Existing regression checks do not authorize
  parallel optimization campaigns or silently expand that boundary.
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
  applications and vendors. Prioritize compute without weakening shared graphics
  semantics. Collaborating products receive no private provider preference.
- Adoption: supporting-feature value, receipts, internal benchmarks, and sibling
  integrations do not establish independent adoption or retained use.
- Deployment: no mandatory browser or cloud service. Platform drivers and
  frameworks remain dependencies; there is no universal zero-dependency or
  zero-allocation promise. Provider substitution must permit safe reversion.
- Diagnostics: report the best established source, validation, resource,
  submission, or native failure location. Never invent a shader location for an
  unattributed hardware fault.
- Work selection: Fawn workspace implementation above Chromium is selected,
  independently of native runtime wins. New compiler opportunities and browser
  replacement require
  explicit selection. Closed browser optimizer experiments remain closed.
  [Ordered milestones](docs/thesis.md#ordered-execution-milestones) do not block
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
- [docs/thesis.md](docs/thesis.md): strategic rationale and sequencing.
- [AGENTS.md](AGENTS.md): contributor instructions and canonical-policy discovery.
- [Documentation index](docs/INDEX.md): architecture, installation, contracts,
  status, and retained evidence.
