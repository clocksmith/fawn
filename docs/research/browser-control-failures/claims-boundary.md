# Browser control, GPU inspection and experiment boundaries

Status: accepted research constraints, not implemented capability claims. Fawn
owns durable actions above stock Chromium; its optional trace cache can reduce
repeated planning only within its admitted coverage. Doe's execution benefits
require separate measurements. GPU inspection is optional and coverage-specific.

## Prior art and comparison

[WebGPU Inspector](https://github.com/brendan-duncan/webgpu_inspector/blob/main/docs/capture.md#pixel-history)
documents pixel history and rendering identifiers, including draw, primitive and
instance information. [WebGPUReconstruct](https://github.com/Chainsawkitten/WebGPUReconstruct)
documents browser capture and native replay using Dawn or wgpu, with known
limitations. These are documented capabilities, not independently qualified
coverage or performance results. They are also retained in the catalogue's
`priorArt` array, separately from failure cases and incident counts.

Claims that wrappers cannot provide these capabilities, or that native replay
alone is a new breakthrough, are withdrawn. Compare supported workloads,
correctness, capture overhead, retained resource coverage and recovery before
claiming a Doe advantage. A replay record needs resource bytes, initialization,
dependencies, pipeline state and ordering; a command-name list is insufficient.

## Attribution and application meaning

An operation identifier is not an application entity or an authorized action.
An entity-ID pass is initially scoped to applications supplying stable mappings,
such as Simulatte and D4DA; this does not claim implemented support in either.
Generic inspection must retain operational identifiers without inventing meaning.

Use a bounded `u32` lookup or explicitly packed identifiers. `rg32uint` contains
two 32-bit unsigned components, not four independently addressable integers.
Declare limits for attachments, blending, alpha coverage, postprocessing and
multisampling. Detect required features rather than assuming support. In
particular, a variant using `primitive_index` requires its applicable feature;
other ID strategies do not inherit that dependency. See the
[WebGPU formats](https://www.w3.org/TR/webgpu/#texture-format-caps) and
[WGSL built-ins](https://www.w3.org/TR/WGSL/#builtin-values).

## Render control and presentation

Headless render control is a hypothesis for each binary/configuration until a
receipt establishes availability. Chrome and `chrome-headless-shell` are
different candidates; Chromium's [headless documentation](https://chromium.googlesource.com/chromium/src/+/main/headless/README.md)
does not establish method availability in every stock Chrome build.

Run the explicit-binary probe in an isolated profile:

```sh
node browser/workspace/test/browser/headless-render-control.js --executable /absolute/browser/binary --out bench/out/new-render-control-probe
```

It records the executable digest, protocol/browser versions, effective arguments
and actual method result. Method absence is `unsupported`; launch, transport,
parameter or invocation failures remain failures. Method presence alone does not
qualify controlled cadence, application readiness or presentation.

Queue completion and screenshot evidence have separate lifetimes. The
[presentation control](../../../browser/workspace/test/browser/gpu-presentation.js)
retains queue receipts and verifies distinctive PNG pixels against each submitted
pattern. A screenshot cannot settle outstanding GPU work, including during
failure cleanup. This bounded control does not prove D4DA terrain, physical
scanout or arbitrary scene semantics.

A D4DA experiment must independently bind its screenshot to the intended scene
revision, application frame/state and camera, then pass an image oracle that
rejects missing terrain, stale frames and incorrect content. Queue settlement,
double animation callbacks or a screenshot response alone do not establish that.

## Scheduling and cancellation

Shared hardware and provider code do not establish shared queues, memory budgets
or process authority. No cross-session scheduler is claimed before explicit
cross-process accounting and admission control exist. Cancellation in the
[reusable-program contract](../../reusable-compute-programs.md) does not preempt
an already running kernel. Cooperative submission boundaries and dispatch
preemption remain different capabilities.

## Redaction

Redaction means a policy applied at capture, with declared pixel coverage and
failure behavior. DOM read grants and DOM hit-testing do not prove screenshot
redaction. Test duplicate text, images, overlays, frames and copies already
painted into canvases. If a sensitive region cannot be located or covered, refuse
the capture or mask a conservatively broader region under the declared policy.
Do not return an unmasked image as a successful redacted capture. No general
canvas-redaction implementation is established by this contract.

## Local planning

The [decision probe](../../../browser/workspace/qualification/planner-decision-probe.js)
compares cloud and loopback-local planners on identical synthetic contexts,
permissions and output grammar. It records per-call wall time, correctness,
interventions, model identities, fixture hashes and failures, with alternating
provider order. Request wall time includes any provider-side preparation; a
preload does not prove that the first request reuses it. Preparation and steady
execution need separate instrumentation before attributing a latency result.
Secrets and real applicant data are not inputs. The historical
3.5–6.9-second range is user-supplied context until source receipts are recovered;
it is not a measured baseline for this probe.

```sh
node browser/workspace/qualification/planner-decision-probe.js --cloud-model MODEL --local-model MODEL --local-url http://127.0.0.1:4878/v1/chat/completions --key-file /private/key/file --out bench/out/new-planner-probe
```

Six decisions cannot establish complete-task reliability, latency percentiles,
replacement quality or performance superiority. Promotion still needs repeated
complete tasks, actual permission/effect boundaries, preparation and fallback
costs, changed-site cases, uncertainty recovery and the competent stock/replay
controls. Planner decisions, cached workflow execution and GPU performance
remain separately measured surfaces.
