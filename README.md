# Doe

Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.

What installs today is DoeRuntime, a native WebGPU runtime for existing GPU
applications, with an independently usable WGSL compiler. Fawn's shared
workspace service is implemented and behaviorally qualified on a frozen case set,
not release-qualified ([status](docs/status/fawn-workspace.md)).
[Three proofs](docs/thesis.md#three-proofs) select current technical work.

**[Package API](packages/doe-gpu/README.md)** · [Evaluate ONNX on Vulkan](docs/onnx-vulkan-installation.md) · [Goals](GOALS.md) · [Documentation](docs/INDEX.md)

![Doe brings compiled WGSL and bound resources together through its selected native backend.](assets/readme/execution.svg)

## Get started

Install the package and run a compute kernel:

```sh
npm install doe-gpu
node node_modules/doe-gpu/examples/node-first-kernel.mjs
```

Check the [supported configurations](docs/doe-support-matrix.md) for host and
driver requirements. For the pinned ONNX integration, download the existing
[evaluation archive](docs/onnx-vulkan-installation.md#download-the-evaluation)
and follow its installation, verification, and execution guide.

## Technical architecture

### Compiler and runtime ownership

![Red preparation branches converge on command recording inside Doe, then reach an external GPU. Completion returns results and gates safe resource retirement.](assets/readme/technical-architecture.svg)

The compiler is independently callable and remains part of Doe's complete WebGPU
implementation. Target code and reflection feed device pipelines; bind groups and
retained resource references feed command recording. Completion governs safe
resource retirement. The application and platform driver/GPU are external.

[Architecture and source owners](docs/architecture.md) · [Prepared-program lifetimes](docs/architecture.md#optional-prepared-program-resource-lifetime)

## Selected direction: a better Doe in C

**Use agent assistance to make explicit implementations affordable, architecture
to make them understandable, and executable enforcement to make them dependable.
C is the selected means; a better Doe is the result that must still be earned.**

C is the chosen migration direction, not an ongoing language contest or a claim
that C is universally safer or faster. The production default remains the retained
Zig implementation. The [experimental C migration](docs/c-migration/MIGRATION.md)
is incremental: accepted compiler and native owners have bounded evidence;
complete feature parity, backend qualification and production cutover remain open.
Publishing an increment does not promote it to the production default.

The architecture should make reusable semantics explicit: one owner per rule,
small typed operations with consistent representations, and clear composition of
ownership, effects, ordering and failures. Reuse follows shared meaning, not
similar spelling or a universal framework. Invariants must be enforced or their
violations detected by checked boundaries, structural checks and independent
behavioral, failure and lifetime tests. Agent-generated code receives the same
review and qualification as any other code; executable checks are not a claim of
formal correctness. The [common C guide](runtime/c/STYLE.md) governs new work.

Keep three objectives separate:
- **Dependency removal:** complete declared builds and consumers without Zig
  implementation, generation, linking or runtime fallback.
- **Engineering improvement:** readable ownership, dependable diagnostics and
  composition, easier native integration, and lower verified maintenance cost.
  Measure unfamiliar maintenance changes rather than assuming agent productivity.
- **Product improvement:** preserve all supported features and earn useful
  application, reliability and adoption gains. Removing a dependency alone does
  not establish these gains.

Each owner earns promotion through its [migration gates](docs/c-migration/MIGRATION.md):
independent behavior and failure evidence, actual consumer integration, qualified
backend/lifetime obligations, and measured maintenance and product benefits.
Retain earlier findings and failed candidates; missing evidence stays unqualified.

The final target is **at least 50% less implementation source with full feature
coverage and improved clarity**, not fewer tests or hidden complexity. The
[fair accounting contract](docs/c-migration/QUALITY_TARGET.md) charges headers,
generated code and its inputs, build/adapter glue and necessary dependency burden,
with test and transitional totals reported separately. Correctness, readability
and qualification cannot be traded for lines. This target is not yet achieved.

## Evidence

A current Doe advantage over Dawn, including complete Doppler generation on
Vulkan, is [not established yet](docs/status/runtime-backends-and-bench.md).

The [ONNX delivery report](reports/releases/20261009-onnx-vulkan-evaluation/README.md)
records bounded SqueezeNet execution and lifecycle checks, including local
isolated installation. Broader compatibility, external reproduction, retained
adoption, and material replacement advantage remain unestablished.

Compiler correctness, runtime performance, and application benefit require
separate evidence. See the [claim index](reports/claim-index.json),
[backend evidence](docs/backend-evidence-matrix.md), and [current status](docs/status.md).
Native results do not qualify browser replacement. Fawn's
[durable workspace contract](docs/fawn-workspace-contract.md) governs its
service; workspace evidence and runtime evidence stay separate. Chromium hooks and the optional
[browser runtime integration](browser/chromium/README.md) are added only when
measured benefits justify their compatibility and maintenance costs; existing
browser evidence stays separate.

## Read next

- [Architecture](docs/architecture.md) — subsystem boundaries and execution details.
- [Goals](GOALS.md) — the environment where agents work: Fawn workspaces, DoeRuntime, DoeLab, and DoeProof.
- [Contributing](AGENTS.md) — repository workflow and component ownership.

## License

[MIT](LICENSE). See [licensing](docs/licensing.md) for third-party boundaries.
