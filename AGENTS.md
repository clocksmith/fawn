# Doe code agent

## Git workflow: direct to main

- Work and commit directly on the owning repository's `main` branch, then push
  directly to its existing `origin/main` remote branch.
- Do not create feature/task branches, branch-backed worktrees, or GitHub pull
  requests. Do not use a branch/PR workflow unless the user explicitly requests it.
- If the checkout is on another branch, preserve its work and move the task to
  `main` safely; never discard changes to switch branches.
- Stage only task-related changes, run the applicable checks, and integrate remote
  updates without overwriting unrelated work. Never force-push `main`.
- Report the pushed commit or the concrete blocker. A local commit is not a push.

## Development verification

- During development, choose checks for the changed behavior: syntax/type checks,
  existing focused tests, or a local visual check. A command list is not a requirement
  to run every suite before each commit, push, or ordinary application deployment.
- Do not automatically run full repository suites, broad browser/device matrices,
  GPU/model campaigns, benchmarks, or full release qualification. Run them when
  explicitly requested or when a named correctness question cannot be answered by
  focused checks; state that reason before starting.
- Once relevant checks pass, continue. Repeat or broaden verification only after
  another relevant change, a failure, or a concrete unresolved concern.
- For documentation-only edits, inspect the diff and changed links. Do not run
  application, GPU, browser, or benchmark suites for prose changes.
- For ordinary deployments, build the changed artifact where needed and check the
  affected route after deployment. Deployment alone does not require a new release
  qualification campaign.
- Keep existing acceptance thresholds and release-qualification requirements.
  Report exactly what was checked; focused development checks do not establish
  numerical qualification, performance, multi-device support, or full acceptance.

## Scope

This file is the source of truth for Doe work only.
Do not apply `dream/AGENTS.md` or `doppler/AGENTS.md` as process for this project.

[GOALS.md](GOALS.md) owns product goals, constraints, and exclusions;
[INTENT.md](INTENT.md) owns system invariants; [the thesis](docs/thesis.md) owns
rationale and sequencing; the applicable [CATSCAN.md](CATSCAN.md) chain owns
component boundaries and acceptance. This file owns contributor instructions
and links technical policy rather than restating it.

## Component intent

Before modifying a file, read every `CATSCAN.md` from the project root to the
target directory, in order. For work spanning directories, read the union of
the applicable chains. Use `rg --files -g CATSCAN.md` and
[`docs/component-index.md`](docs/component-index.md) to discover the chain.

Treat `Target`, `Authority`, `Scope`, `Contracts`, `Invariants`, `Acceptance`, and `Non-goals` as
implementation constraints. A child `CATSCAN.md` may narrow its parent but may
not contradict it, broaden its authority, or weaken an invariant. Existing code
does not overrule a charter; code may itself have drifted.

If requested work changes a component boundary, identify the conflict and
update the affected `CATSCAN.md` with the implementation. Do not silently work
around it. Explicit user direction may change intent, but the corresponding
charter and acceptance evidence must change in the same work. Do not rewrite a
charter to excuse a failing implementation, test, or receipt.

Novel implementations are welcome. `CATSCAN.md` constrains outcomes and
authority, not internal algorithms. Do not add a charter to a utility folder or
mechanical subdivision that can inherit its nearest parent.

Agent handoffs for component-changing work must state:

```text
Component: <name>
Intent: preserved | changed
Acceptance evidence: <commands/artifacts>
Boundary effects: <none or named components>
```

## Tooling surface contract

Canonical public/internal/archive tooling separation is defined in:

- `config/tool-surfaces.json`
- `docs/internal-tooling.md`

Default assumptions:

- only `packages/doe-gpu/` exports and docs are the public npm package contract
- `bench/`, `browser/chromium/`, `pipeline/`, top-level `scripts/`, and
  contributor tooling under `runtime/zig/` are repo-only unless the tooling
  manifest marks them `audience=public`
- legacy npm names `@simulatte/webgpu` and `@simulatte/webgpu-doe` redirect to
  `doe-gpu`
- `pipeline/dawn-research/` is a Gerrit CL analysis pipeline (research surface; see
  `pipeline/agent/README.md`) referenced by `config/tool-surfaces.json`
- `docs/archive/nursery/` retains archive navigation; current CI does not consume
  executable surfaces there. Chromium integration lives in `browser/chromium/`, and CTS
  provider tooling lives in `bench/cts/`. Use `config/tool-surfaces.json` and
  current workflow paths when classifying a surface.

Do not infer public product commitments from repo-only tools, scripts, or
historical docs.

## Mandatory reading

Before changing Doe behavior, read:

- `GOALS.md`
- `INTENT.md`
- the applicable root-to-target `CATSCAN.md` chain
- `docs/thesis.md`
- `docs/architecture.md`
- `docs/process.md`
- `docs/status.md`
- `docs/upgrade-policy.md`
- `docs/licensing.md`
- `pipeline/agent/README.md`
- `pipeline/lean/README.md`
- `runtime/zig/README.md`
- `runtime/zig/STYLE.md`
- `bench/README.md`
- `pipeline/trace/README.md`

If a change affects runtime-visible behavior and any mandatory doc above has not been read in the current task, stop and read it before editing code.

For Dawn-vs-Doe performance work, also read:

- `SKILLS.md`
- `docs/performance-strategy.md`

For any Cerebras lane work (Doppler → Doe → Cerebras), start at
`docs/cerebras.md`. That is the single front door — progress snapshot,
source-code locations, reproduce/build/verify commands, hardware runbook
pointer, and rationale all in one page. The bundle packer and
claim-discipline gate depend on `docs/cerebras-evidence-bundle.md` and
`docs/hardware-validation-appendix.md`; do not delete or rename those.

## Current work selection

Follow [GOALS.md's constraints](GOALS.md#strategic-constraints) and
[the thesis's current focus](docs/thesis.md#focus-and-founder-responsibilities).
Consult the [ONNX installation guide](docs/onnx-vulkan-installation.md) for the
existing evaluation distribution; do not repeat completed substitution discovery.
Do not add Chromium hooks or browser-runtime integration without the
measured-benefit selection [GOALS.md](GOALS.md#strategic-constraints) requires.
Selecting work does not promote evidence or reopen rejected campaigns.

## Canonical technical policies

Read and apply the owner relevant to the change; do not copy its policy here:

- System identity, fail-closed behavior, lifecycle safety, config/schema,
  placeholders, synthetic-state rejection, and offline promotion:
  [INTENT.md](INTENT.md).
- Stages, blocking gates, proof admission, artifact/trace requirements, and
  refactor acceptance: [process law](docs/process.md).
- Runtime/compiler boundaries and Zig-first proof elimination:
  [architecture](docs/architecture.md#verification-boundary) and the charter chain.
- Comparison classes, structural work, timing scope, statistical reliability,
  and suspicious-result audits: [performance contract](docs/performance-strategy.md).
- Public/internal/archive surface classification:
  [tooling manifest](config/tool-surfaces.json) and [guide](docs/internal-tooling.md).

A contract change must update its canonical policy, schemas/migration notes,
applicable gates, and status in the same change. References never waive a rule.

## Intent-first operations

- Treat Doe intent as durable workspace authority and source-preserving
  compiler/runtime evidence, not package copy or benchmark prose. Fawn workspace
  qualification is separate from hosted artwork and Chromium runtime receipts.
- Use `Fawn` alone only for the durable browser workspace product. Name the
  forced-Doe Chromium build, hosted demonstrations, and Fawn matrix explicitly;
  [the glossary](docs/glossary.md) owns these terms.
- If the user asks whether a Doe claim is true, inspect the current artifacts, gates, raw benchmark rows, browser smoke outputs, and source files before answering.
- For publish-readiness or stale-claim questions, inspect `reports/claim-index.json` and every referenced sidecar before answering.
- When a Dawn-vs-Doe comparison is distrusted, fix or harden the benchmark fairness gate before changing README/docs language.
- Do not answer that Doe beats Dawn from `claimStatus` alone; verify same work, same timing scope, non-missing dispatch/readback work, and raw timing sign.
- For Doe-vs-Dawn performance questions, answer row by row with: row id, backend, comparator, p50 sign, p95 sign, claim status, artifact path, and caveat.
- If the user asks for a browser proof path, distinguish npm package status from a forced-Doe Chromium artifact and report the exact build/output state.
- For publish readiness, split the answer into: npm package, native runtime evidence, forced-Doe Chromium artifact, docs/charts, and npm auth.
- Do not frame publish work as blocked before checking local package files, release artifacts, claim index rows, sidecars, docs/charts, and auth state.
- When the user says a diagnostic release row must be claimable, treat it as a failing release criterion: identify the measured loss or tail-stability cause, patch or rerun, then update the claim index from artifacts.
- If the user asks for real browser/demo benchmarks, inventory existing particle/WebGPU HTML demos, verify whether they run in the forced-Doe Chromium build, and name the benchmark harness needed before discussing evidence boundaries.
- Public claims must point to artifact paths or release files. Prose must not substitute for missing evidence.
- If a result looks suspicious, say so directly and treat it as a harness or methodology problem until the artifact proves otherwise.

## Systematic Zig review

For systematic Zig cleanup, use [`runtime/zig/reviews/README.md`](runtime/zig/reviews/README.md)
and its append-only log. Begin with `python3 runtime/zig/tools/review_log.py --next`;
`config/zig-review-plan.json` owns the bounded active and ready batches, while
`queue.tsv` retains complete coverage rather than prescribing alphabetical work.
Review each file, directory organization, relationships
within directories, cross-directory boundaries, and complete execution paths as
separate passes. Existing module decisions and passing gates do not grant code
review credit. Partial inspection remains unfinished; higher-level completion
requires current lower-level reviews and its own findings and evidence.

Append findings, fixes, verification artifacts, and the next concrete action
before handing off a review. Regenerate and check the queue with
`python3 runtime/zig/tools/review_log.py --write` and `--check`. When checking a
committed change, pass `--base-ref` with its predecessor or an explicitly selected
baseline on `main` to enforce history preservation across the change. Source ownership policy remains in the
existing charter chain and architecture manifest.

## Implementation style

- keep modules small, composable, and explicit in data flow
- avoid one-file catchall utilities
- prefer pure transforms for deterministic stages
- minimize inline commentary; use field names and namespaced constants for intent
- if adding selection logic, prefer rule-map data over branching ladders

## Documentation drift prevention

- Evidence retention follows [reports/README.md](reports/README.md). Routine
  checks and debugging output go to ignored `bench/out/`; a successful test or
  visual tweak does not require a report folder, README, or archived status entry.
  Reusable harnesses belong in the owning test/tool directory, not reports.
- never embed counts, percentages, or benchmark results in prose; reference the artifact path
- structural claims ("Doe has a native DXIL emitter") are validated by CI against file existence
- when a doc needs a specific number, use the pattern: "See `path/to/artifact.json` for current count."
- `docs/status.md` is the concise status front door; live status details live in topical shards under `docs/status/*.md`, and dated append-only history lives in `docs/status/archive/*.md`
- new state goes at the top of the relevant topical shard; live shards are LOC-capped and must be split by subdomain before they grow past the cap, and old archive entries are not edited except for deliberate archive maintenance
- artifacts are the source of truth, not docs:
  - theorem count/categories: `pipeline/lean/artifacts/proven-conditions.json`
  - benchmark results: `bench/out/*/dawn-vs-doe.*.json`
  - browser smoke: `browser/chromium/artifacts/*/dawn-vs-doe.browser.playwright-smoke.diagnostic.json`
  - backends: `runtime/zig/src/backend/*/`
  - test status: `zig build test-wgsl` exit code
- prose describes what kind of thing exists, not how many; the artifact has the count

## Documentation style

- Markdown document titles and section headings use sentence case
- Use the [documentation ownership map](docs/INDEX.md#canonical-strategy).
  Consolidate repeated sections into their canonical owner before considering
  file deletion. Preserve concrete architecture, installation, and contract
  details and admitted evidence under the retention policy. Historical routine
  runs may leave the working tree when their immutable Git history remains linked.

## File size

- `runtime/zig/source-layout.json` is the source of truth for Zig source size policy
- 800 lines is an advisory architecture review signal
- handwritten production files above 1,200 lines require a cohesive-module justification in the architecture manifest
- handwritten production files above 1,500 lines fail the gate
- generated files use separately declared reproducible generation contracts
- split by named semantic responsibility, not by arbitrary line count
- group by feature (e.g. `pipeline_cache.zig`) not by type (e.g. `helpers.zig`)
- keep related code together; splitting a file must not scatter a single concern
- Python benchmark and tooling files must stay modular; when a file exceeds 1200 lines, add a tracked sharding follow-up in the relevant live topical shard with owner and next split target.

## Constants and thresholds

- inline `0`, `1`, simple indexing values, and language-level sentinels when
  they express local mechanics clearly
- name domain, ABI, policy, threshold, size, retry, and timing values
- centralize thresholds in config or module-level constants, not inline literals
- if a value appears in more than one place, it must have a single source of truth
- fallbacks must reference named constants or config getters, never bare literals

## Error handling

- fail fast on invalid inputs with descriptive messages
- unsupported operations return explicit taxonomy errors, not silent no-ops
- include actionable context: what was expected, what was received

## Comments

- comments explain why, not what
- do not add comments that restate the code
- do not add ad-hoc debug logging; use structured trace output

## No speculative engineering timelines

- Do not predict how long a coding, software-engineering, product-implementation, refactor, migration, launch, or similar work item will take. Avoid speculative delivery statements such as "1-2 weeks", "four months", or "a quick fix".
- Describe planned work through concrete deltas, dependencies, risks, and validation instead of calendar duration.
- This restriction does not apply to factual status for an already-running command, script, benchmark, training run, skill, deployment, or algorithm. You may report elapsed time, measured runtime, progress, and a grounded ETA when the active process exposes enough evidence.
- Do not invent an ETA for an active process. If it does not expose one, report its current phase, latest output, and whether it is still making progress.

## Style guides

Each language has a dedicated style guide. Read the relevant guide before
editing code in that surface:

- Zig: [`runtime/zig/STYLE.md`](runtime/zig/STYLE.md)
- JavaScript: [`packages/doe-gpu/STYLE.md`](packages/doe-gpu/STYLE.md)
- JSON/config: [`config/STYLE.md`](config/STYLE.md)
- Python: [`bench/STYLE.md`](bench/STYLE.md)
- Lean: [`pipeline/lean/STYLE.md`](pipeline/lean/STYLE.md)

## Benchmark style

Apply the [performance contract](docs/performance-strategy.md) before accepting
or reporting any timing result. Use its comparison-class, work-equivalence,
timing-scope, upload, and fairness-audit requirements; inspect the raw artifacts
rather than relying on `claimStatus` alone. A failed or suspicious comparison
stays diagnostic until its obligations are established.

## Benchmark front doors

- first-time benchmark execution instructions live in `bench/README.md` under
  `First benchmark matrix`; use that matrix instead of inferring support from
  scattered config filenames
- prefer `python3 bench/cli.py compare` with promoted profiles when available;
  verify the current promoted matrix with `python3 bench/cli.py compare --list-promoted`
- current front-doored coverage is narrower than the full taxonomy:
  - backend native Doe-vs-Dawn is front-doored on `apple-metal`,
    `amd-vulkan`, and `local-d3d12`
  - plan compare is currently front-doored on `apple-metal` only
  - package compare is currently front-doored on `apple-metal`, `amd-vulkan`,
    and `local-d3d12` for Node/Bun cold and warm Gemma package lanes
  - AMD Vulkan Gemma270m package compares remain explicit config-backed files,
    not promoted `--surface package` profiles
  - local D3D12 package profiles are promoted contracts; claim evidence still
    requires a compatible Windows/D3D12 host
- do not assume every `platform x surface x runtimeHost` tuple named in the
  taxonomy is promoted or evidenced; check the promoted list or an explicit
  compare config path before benchmarking

## Completion checklist

For each change set, verify:

- schema updates and migration notes are consistent
- docs in this repo reflect behavior
- gate expectations were updated or confirmed in `docs/process.md`
- pipeline/trace/replay outputs are consistent with the changed behavior
- if Dawn-vs-Doe benchmarking changed, apples-to-apples methodology is documented and enforced by fail-fast checks
- if any workload is marked claimable, verify required work, timing completeness,
  declared fixed-operation controls or substantiated application-level elimination,
  hardware scope, and the fairness audit for any unusually large speedup
- the status log (`docs/status.md` plus the relevant topical shard) records remaining placeholders, temporary methodology choices, and follow-up work
- update live status when a boundary or blocker changes; do not archive routine
  edits or passing checks just to document activity

## Pick the real fix

- when you find a correctness bug, the default is to fix it, not to relabel it
- do not use effort or scope framing ("non-trivial", "real engineering effort", "worth its own thread", "we'll address later") as cover for choosing a lesser fix
- do not propose "mark experimental", "add a TODO", or "rewrite the misleading comment" as a substitute for the actual engineering work when the underlying behavior is wrong
- if scope genuinely must be split, describe the concrete deltas and ask the user which path to take, do not pre-decide a smaller version
