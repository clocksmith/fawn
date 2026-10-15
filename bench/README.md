# Doe benchmark and evidence tooling

`bench/` owns repository-only correctness, compatibility, reliability,
performance, comparison, and claim tooling. It is not part of the public npm
package.

## Start here

```bash
python3 bench/cli.py --help
python3 bench/cli.py workload --help
python3 bench/cli.py compare --help
python3 bench/cli.py claim --help
python3 bench/cli.py external --help
python3 bench/cli.py program --help
```

Use [`docs/operator-runbook.md`](../docs/operator-runbook.md) for platform
procedures and [`bench/docs/benchmark-writing-guide.md`](docs/benchmark-writing-guide.md)
for adding workloads.

External application reproduction starts at
[`external-projects/README.md`](external-projects/README.md). The
`external reproduce` command owns source preparation, host admission, Doe
build identity, policy gates, workload execution, and receipt routing; a
passing command is still not a public claim.

Declared compute programs use `program candidate`, `program prepare-lif`, `program evaluate`,
`program qualify-package`, `program verify`, and `program verify-native`.
Use `program evaluate --package-qualification <summary.json>` to compare
applications using the exact archives retained by package qualification.
Install the pinned comparator dependencies with `npm ci --prefix bench`.
Their policy and evidence boundaries are documented in
[`reusable-compute-programs.md`](../docs/reusable-compute-programs.md).
Investigate ordinary application warmup and host costs through
[`measurement diagnosis`](../docs/compute-program-resolution.md); its profiles
remain separate from candidate acceptance.

`program candidate` evaluates WGSL against an independently pinned job containing
a trusted CPU reference, frozen numerical oracles, input files, resource limits,
and performance acceptance. See the [candidate workflow](../docs/reusable-compute-programs.md#bounded-candidate-jobs).

## Choose the benchmark

Optimization attention follows this order. Correctness, resource safety and
measurement calibration apply throughout. The development loop is application
comparison → focused attribution → one correction → application confirmation →
independent transfer. Vulkan is the selected backend; other backend recipes remain
discoverable without opening parallel campaigns.

| Priority and question | Existing owner and recipe | Result location and role |
| --- | --- | --- |
| Doppler Node generation: does provider replacement improve inference? | [Generation harness](external-projects/doppler-generation/README.md); `generation` for installed qualification, `generation-memory` for the existing memory cohort; the owner documents latency cohorts | `bench/out/doppler-generation/`, reviewed `reports/benchmarks/amd-vulkan/` and `reports/maintenance/`; primary application confirmation |
| ONNX: does the same correction transfer outside Doppler? | [Source-matched campaign](external-projects/onnx-vulkan-campaign/README.md); `onnx-measure` after its preparation and safety recipes | `bench/out/onnx-vulkan-campaign/`, reviewed `reports/benchmarks/amd-vulkan/`; independent transfer through existing operators |
| Compiler/runtime attribution: which mechanism causes the difference? | [Matrix recipe](../reports/maintenance/20261011-intel-vulkan-projection/README.md) and [AMD projection recipe](../reports/maintenance/20261012-amd-projection-cost/README.md); `projection-cost` with explicit policy and retained inputs | Caller-selected new output directory under `bench/out/`; reviewed `reports/maintenance/`; focused diagnosis, not inference acceptance |
| Standalone compiler: are compilation and diagnostics useful independently? | [Compiler comparator](native-compare/compare_doe_vs_tint_compilation.py); `compile-compare --config bench/native-compare/compare_doe_vs_tint.config.json`; semantic tests remain in `runtime/zig/` | Configured report path; compilation cost separate from generated-program speed |
| Runtime primitives: which application cost can be removed? | [Native profiles](native-compare/) and [promoted catalog](../config/promoted-compare-catalog.json); `compare --list-promoted --backend amd-vulkan`, then explicit one-side runs | Configured `bench/out/` receipts; allocation, binding, submission, synchronization and readback attribution |
| General compute/rendering: does the correction preserve other users? | [UMAP harness](external-projects/umap-gpu/sgd-output-correctness.harness.json), [workload suite](../config/doe-workload-suite.json); `external reproduce --dry-run` with the selected harness or `workload --workload-id <id>` | `bench/out/external-projects/`, `bench/out/workloads/`; affected-path correctness now, separately qualified application value later |

All commands above start with `python3 bench/cli.py`. Inspect delegated arguments:

```bash
python3 bench/cli.py generation --help
python3 bench/cli.py generation-memory --help
python3 bench/cli.py onnx-measure --help
python3 bench/cli.py projection-cost --help
python3 bench/cli.py compile-compare --help
python3 bench/cli.py graphics --help
```

These dispatches preserve existing implementations, dependencies, explicit inputs,
output paths and exit codes. Use the owning recipe's Python environment where
required. They do not create new policies or reopen rejected candidates.
An available profile, an executed result and an accepted advantage are distinct;
`compare --list-promoted` discovers contracts, not current wins.

For the next latency campaign, predeclare short-prompt/substantial-output,
longer-prompt/short-output and resident-repeat cases in one independently checked
model configuration. Freeze installed package, weights, tokenizer, shaders,
prompts, capacity, sampling and stopping across providers. Complete generation
is primary; retain first-token latency, token timing, tails, CPU and memory.
Opening, first request, resident requests and settlement have separate boundaries.
Declare filesystem and driver-cache conditions; distinguish requested bytes,
retained capacity, process RAM and driver memory. Existing first-token and memory
policies keep their original metrics and verdicts. Quality-rejected W8A8 remains
diagnostic and cannot qualify model replacement.

Start attribution with Tint and Doe output inside Doe. Dawn's retained SPIR-V
import retranslates through Tint; the matrix also records an unsupported packed
sequence. Neither is a clean same-shader runtime comparison. Portable versus
packed arithmetic is a separate treatment. Retain required status readback and
completion, quantify instrumentation perturbation and confirm without observers.
Compilation process startup and resident compilation are separate boundaries;
unsupported shaders and malformed-input failures remain visible. Generated-program
performance does not belong in the compilation-latency result.

After a credible application improvement, test ONNX transfer only where it
exercises the changed mechanism. Keep its operators, independent full-output
oracle and disabled CPU fallback. General-compute/render smoke and forced-Doe Chromium
visuals cannot stand in for a native application speed result.

## Available backend recipes

Use the physical backend runner matching the host. It performs host preflight,
separate Doe and Dawn runs, strict receipt comparison, output-oracle checks,
and cumulative recomposition evidence capture:

| Host/backend | Command |
| --- | --- |
| Linux / AMD Vulkan | `python3 bench/runners/run_recomposition_backend_evidence.py --backend vulkan` |
| macOS / Apple Metal | `python3 bench/runners/run_recomposition_backend_evidence.py --backend metal` |
| Windows / D3D12 | `python3 bench/runners/run_recomposition_backend_evidence.py --backend d3d12` |

Inspect the full promoted surface before choosing a broader run:

```bash
python3 bench/cli.py compare --list-promoted
```

The runner requires built Doe and Dawn native artifacts. It fails preflight
with the missing dependency instead of substituting another backend.

## Evidence flow

The required order is:

1. define a workload and independent output oracle;
2. run each provider separately and retain raw artifacts;
3. verify runtime identity, successful work, output, and synchronization;
4. verify equivalent required work under the predeclared comparison class;
5. evaluate claim policy separately;
6. publish only reviewed, claim-eligible artifacts.

Timing from an invalid or unequal execution is diagnostic.

## Core contracts

- Workload identity and execution adapters:
  [`docs/workload-system.md`](../docs/workload-system.md)
- Compare vocabulary and axes:
  [`docs/benchmark-taxonomy.md`](../docs/benchmark-taxonomy.md)
- Correctness and performance methodology:
  [`docs/performance-strategy.md`](../docs/performance-strategy.md)
- Release stage and failure precedence:
  [`docs/process.md`](../docs/process.md)
- Public wording:
  [`docs/public-claim-boundary.md`](../docs/public-claim-boundary.md)

Configuration and schemas own thresholds, allowed values, workload catalogs,
backend policies, and claim rules. Do not copy mutable values into this README.

## Directory map

| Path | Responsibility |
| --- | --- |
| `bench/cli.py` | Stable operator front door |
| `bench/workloads/` | Workload definitions, assets, and metadata |
| `bench/oracles/` | Independent correctness oracles |
| `bench/runners/` | Governed run and cohort orchestration |
| `bench/executors/` | Workload execution in native and application hosts |
| `bench/native_compare_modules/` | Comparison implementation used by the operator front door |
| `bench/native-compare/` | Declared native comparison profiles |
| `bench/browser/` | Browser smoke and release evidence validation |
| `bench/gates/` | Blocking and advisory decisions |
| `bench/tools/` | Builders, checkers, and reports |
| `bench/docs/` | Focused operator and integration references |
| `bench/out/` | Generated run artifacts and receipts |

Keep operator commands in `bench/cli.py`; executors and comparison modules
implement those commands rather than defining competing front doors. Standalone
diagnostics retain their declared recipes and cannot promote a result themselves.
Repository health glue lives in `scripts/`; benchmark builders, artifact readers,
and reporting helpers belong in `bench/tools/`.

`bench/out/` includes retained evidence and pinned dependencies, not just scratch
files. Establish references and custody before deleting a run. The CI cleanup
tool, `bench/tools/cleanup_out.py`, applies retention rules to its runner output;
it does not establish that a local evidence archive is safe to prune.

`config/tool-surfaces.json` owns whether any tool is public or repo-only.
Browser release receipt ownership and existing admission commands are mapped in
[`browser/README.md`](browser/README.md). Shared admission policy stays in the
gates; artifact-family validation belongs to the workflow that consumes it.

## Comparability rules

Declare the [comparison class](../docs/performance-strategy.md#comparison-classes)
before freezing inputs. A comparable row requires matched:

- source workload and inputs;
- output oracle;
- requested computation and numerical/validation requirements;
- application-level preparation, reuse and cache population opportunities;
- visibility, ordering, completion, lifetime and failure requirements;
- hardware and driver environment;
- timing scope and normalization;
- sample policy.

Application replacement preserves those obligations and complete measurement
boundaries; internal allocations, copies, submissions, caching and generated
programs may differ. Fixed-operation experiments additionally preserve command,
dispatch, repeat, preparation and effective-path controls needed to isolate the
declared treatment. Verified elimination of unnecessary work can be an advantage;
missing required work, unchecked output, omitted costs and unknown provider or
fallback identity invalidate either class.

Existing frozen contracts, executable gates and historical verdicts remain
unchanged. A new application comparison needs versioned obligations and matching
executable checks; this prose cannot override a gate that enforces fixed paths.

## Performance reporting

Report the complete user-visible operation first. Phase timing is attribution,
not a substitute. Keep cold and warm runs separate, include latency tails and
memory, and record failures, retries, and fallbacks beside performance.

Suspiciously large wins trigger a fairness audit. Hardware-specific elimination
and different readback mechanisms require verified equivalent application
semantics, complete costs and explicit hardware scope. Differences outside a
fixed-operation treatment remain diagnostic.

## Artifacts

Generated runs belong under grouped `bench/out/<lane>/<run-id>/` directories.
Stable reviewed reports belong under `reports/`. Public rows enter
`reports/claim-index.json` only after their report and claim sidecar are
reviewed and current.

Do not cite scratch paths as durable evidence. Do not edit generated artifacts
to change a verdict; fix the producer, workload, or gate and rerun it.

## Release checks

The canonical gate runners live under `bench/runners/`. Hardware-specific
lanes may remain manual, but a promoted release must record which required
checks ran, which were unavailable, and why. Optional flags are not evidence
that a check protected the release.

## Focused references

- [`bench/docs/benchmark-writing-guide.md`](docs/benchmark-writing-guide.md)
- [`bench/docs/dawn-delegate-cache-integration.md`](docs/dawn-delegate-cache-integration.md)
- [`bench/docs/vulkan-pipeline-cache-integration.md`](docs/vulkan-pipeline-cache-integration.md)
- [`bench/docs/operator-diff-demo-runbook.md`](docs/operator-diff-demo-runbook.md)
- [`bench/tools/cerebras-evidence-bundle-tools.md`](tools/cerebras-evidence-bundle-tools.md)

Historical commands and benchmark findings belong in status archives and git
history, not this entrypoint.
