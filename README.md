# Doe

Doe builds the environment where agents work. Fawn provides durable browser
workspaces; DoeRuntime supplies independently useful GPU execution; DoeLab tests
improvements; DoeProof records evidence.

What installs today is DoeRuntime, a native WebGPU runtime for existing GPU
applications, with an independently usable WGSL compiler. Vulkan is the current
engineering focus. Fawn's shared workspace service is the next build and is
[not implemented yet](docs/status/fawn-workspace.md).

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

## Evidence

DoeRuntime engineering is focused on one correct, reproducible Doppler Node
generation advantage over Dawn on Vulkan, then transfer to ONNX. That advantage
is [not established yet](docs/status/runtime-backends-and-bench.md).

The [ONNX delivery report](reports/releases/20261009-onnx-vulkan-evaluation/README.md)
records bounded SqueezeNet execution and lifecycle checks, including local
isolated installation. Broader compatibility, external reproduction, retained
adoption, and material replacement advantage remain unestablished.

Compiler correctness, runtime performance, and application benefit require
separate evidence. See the [claim index](reports/claim-index.json),
[backend evidence](docs/backend-evidence-matrix.md), and [current status](docs/status.md).
Native results do not qualify browser replacement. Fawn's
[durable workspace contract](docs/fawn-workspace-contract.md) has no implemented
shared service or behavioral acceptance yet. Chromium hooks and the optional
[browser runtime integration](browser/chromium/README.md) are added only when
measured benefits justify their compatibility and maintenance costs; existing
browser evidence stays separate.

## Read next

- [Architecture](docs/architecture.md) — subsystem boundaries and execution details.
- [Goals](GOALS.md) — the environment where agents work: Fawn workspaces, DoeRuntime, DoeLab, and DoeProof.
- [Contributing](AGENTS.md) — repository workflow and component ownership.

## License

[MIT](LICENSE). See [licensing](docs/licensing.md) for third-party boundaries.
