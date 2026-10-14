# Doe code agent

## Git workflow: direct to main

- Commit on the owning repo's `main` and push to `origin/main`. No feature
  branches, branch-backed worktrees, or PRs unless the user asks for them.
- On another branch, preserve its work and move the task to `main` safely;
  never discard changes to switch. Never force-push `main`.
- Stage only task-related changes; integrate remote updates without
  overwriting unrelated work.
- Report the pushed commit or the concrete blocker. A local commit is not a push.

## Development verification

- During development, choose checks for the changed behavior: syntax/type checks,
  existing focused tests, or a local visual check. A command list is not a requirement
  to run every suite before each commit, push, or ordinary application deployment.
- Do not automatically run full repository suites, broad browser/device matrices,
  GPU/model campaigns, benchmarks, or full release qualification. Run them when
  explicitly requested or when a named correctness question cannot be answered by
  focused checks; state that reason before starting.
- Config-contract, mirror, drift, and freshness checks run only when the change
  edits what they check. Don't add tests that just restate another file or pin
  config literals. Time-box long processes.
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

This file is the source of truth for Doe work only; do not apply
`dream/AGENTS.md` or `doppler/AGENTS.md` here.

[GOALS.md](GOALS.md) owns product goals, constraints, and exclusions;
[INTENT.md](INTENT.md) owns system invariants; [the thesis](docs/thesis.md) owns
rationale and sequencing; the applicable [CATSCAN.md](CATSCAN.md) chain owns
component boundaries and acceptance. This file owns contributor instructions
and links technical policy rather than restating it.

## Component intent

Before modifying a file, read every `CATSCAN.md` from the project root to the
target directory, in order (the union of chains for cross-directory work);
discover them with `rg --files -g CATSCAN.md` and
[`docs/component-index.md`](docs/component-index.md).

`Target`, `Authority`, `Scope`, `Contracts`, `Invariants`, `Acceptance`, and
`Non-goals` are implementation constraints. A child `CATSCAN.md` may narrow its
parent but not contradict it, broaden its authority, or weaken an invariant.
Existing code does not overrule a charter; code may have drifted.

If requested work changes a component boundary, name the conflict and update
the affected `CATSCAN.md` with the implementation instead of silently working
around it. Explicit user direction may change intent; the charter and
acceptance evidence then change in the same work. Never rewrite a charter to
excuse a failing implementation, test, or receipt.

Charters constrain outcomes and authority, not internal algorithms; novel
implementations are welcome. Do not add a charter to a utility folder or
mechanical subdivision that can inherit its nearest parent.

Agent handoffs for component-changing work must state:

```text
Component: <name>
Intent: preserved | changed
Acceptance evidence: <commands/artifacts>
Boundary effects: <none or named components>
```

## Tooling surface contract

`config/tool-surfaces.json` and `docs/internal-tooling.md` define the
public/internal/archive tooling split. Defaults:

- only `packages/doe-gpu/` exports and docs are the public npm package
  contract; legacy npm names `@simulatte/webgpu` and `@simulatte/webgpu-doe`
  redirect to `doe-gpu`
- `bench/` (CTS provider tooling in `bench/cts/`), `browser/chromium/`
  (Chromium integration), `pipeline/`, top-level `scripts/`, and contributor
  tooling under `runtime/zig/` are repo-only unless the manifest marks them
  `audience=public`
- `pipeline/dawn-research/` is a Gerrit CL analysis research surface (see
  `pipeline/agent/README.md`)
- `docs/archive/nursery/` is archive navigation; current CI consumes no
  executable surfaces there
- classify surfaces by the manifest and current workflow paths; never infer
  public product commitments from repo-only tools, scripts, or historical docs

## Mandatory reading

Before changing Doe behavior, read in the current task (before editing code):

- `GOALS.md`, `INTENT.md`, and the `CATSCAN.md` chain (Component intent)
- `docs/thesis.md`, `docs/architecture.md`, `docs/process.md`,
  `docs/status.md`, `docs/upgrade-policy.md`, `docs/licensing.md`
- `pipeline/agent/README.md`, `pipeline/lean/README.md`,
  `pipeline/trace/README.md`
- `runtime/zig/README.md`, `runtime/zig/STYLE.md`, `bench/README.md`

For Dawn-vs-Doe performance work, also read `SKILLS.md` and
`docs/performance-strategy.md`.

For Cerebras lane work (Doppler → Doe → Cerebras), start at the single front
door `docs/cerebras.md` (status, source, commands, hardware runbook pointer).
The bundle packer and claim-discipline gate depend on
`docs/cerebras-evidence-bundle.md` and `docs/hardware-validation-appendix.md`;
do not delete or rename those.

## Current work selection

Follow [GOALS.md's constraints](GOALS.md#strategic-constraints) and
[the thesis's current focus](docs/thesis.md#focus-and-founder-responsibilities);
Chromium hooks or browser-runtime integration need the measured-benefit
selection GOALS.md requires. Consult the
[ONNX installation guide](docs/onnx-vulkan-installation.md) for the existing
evaluation distribution; do not repeat completed substitution discovery.
Selecting work does not promote evidence or reopen rejected campaigns.

## Canonical technical policies

Apply the owner relevant to the change:

- System identity, fail-closed behavior, lifecycle safety, config/schema,
  placeholders, synthetic-state rejection, and offline promotion:
  [INTENT.md](INTENT.md).
- Stages, blocking gates, proof admission, artifact/trace requirements, and
  refactor acceptance: [process law](docs/process.md).
- Runtime/compiler boundaries and Zig-first proof elimination:
  [architecture](docs/architecture.md#verification-boundary) and the charter chain.
- Comparison classes, structural work, timing scope, statistical reliability,
  and suspicious-result audits: [performance contract](docs/performance-strategy.md).
- Public/internal/archive surface classification: see Tooling surface contract.

A contract change must update its canonical policy, schemas/migration notes,
applicable gates, and status in the same change. References never waive a rule.

## Intent-first operations

- Doe intent is durable workspace authority and source-preserving
  compiler/runtime evidence, not package copy or benchmark prose. Fawn workspace
  qualification is separate from hosted artwork and Chromium runtime receipts.
- `Fawn` alone means only the durable browser workspace product; name the
  forced-Doe Chromium build, hosted demonstrations, and Fawn matrix explicitly
  ([the glossary](docs/glossary.md) owns these terms).
- Before saying whether a Doe claim is true, inspect current artifacts, gates, raw benchmark rows, browser smoke outputs, and source files; for publish-readiness or stale claims, also `reports/claim-index.json` and every referenced sidecar.
- When a Dawn-vs-Doe comparison is distrusted, fix or harden the benchmark fairness gate before changing README/docs language.
- Never say Doe beats Dawn from `claimStatus` alone; verify same work, same timing scope, non-missing dispatch/readback work, and raw timing sign. Answer row by row: row id, backend, comparator, p50 sign, p95 sign, claim status, artifact path, caveat.
- For a browser proof path, distinguish npm package status from a forced-Doe Chromium artifact and report the exact build/output state.
- Split publish-readiness answers into npm package, native runtime evidence, forced-Doe Chromium artifact, docs/charts, and npm auth; check each, including local package files, release artifacts, claim index rows, and sidecars, before calling publish blocked.
- A diagnostic release row the user says must be claimable is a failing release criterion: find the measured loss or tail-stability cause, patch or rerun, then update the claim index from artifacts.
- For real browser/demo benchmark requests, inventory existing particle/WebGPU HTML demos, check whether they run in the forced-Doe Chromium build, and name the needed harness before discussing evidence boundaries.
- Public claims point to artifact paths or release files; prose never substitutes for missing evidence.
- Call a suspicious result suspicious; treat it as a harness or methodology problem until the artifact proves otherwise.

## Systematic Zig review

For systematic Zig cleanup, use [`runtime/zig/reviews/README.md`](runtime/zig/reviews/README.md)
and its append-only log, starting with `python3 runtime/zig/tools/review_log.py --next`.
`config/zig-review-plan.json` owns the bounded active and ready batches;
`queue.tsv` keeps complete coverage, not an alphabetical work order.
Files, directory organization, within-directory relationships, cross-directory
boundaries, and complete execution paths are separate passes. Existing module
decisions and passing gates grant no review credit; partial inspection is
unfinished; higher-level completion needs current lower-level reviews plus its
own findings and evidence.

Only when handing off a review (not for ordinary code changes): append
findings, fixes, verification artifacts, and the next concrete action, then
run `python3 runtime/zig/tools/review_log.py --write` and `--check`. For a
committed review change, pass `--base-ref` with its predecessor or a selected
baseline on `main` to enforce history preservation.

## Implementation style

- keep modules small, composable, and explicit in data flow; avoid one-file
  catchall utilities
- prefer pure transforms for deterministic stages
- minimize inline commentary; use field names and namespaced constants for intent
- if adding selection logic, prefer rule-map data over branching ladders

## Documentation drift prevention

- Evidence retention follows [reports/README.md](reports/README.md): routine
  checks and debugging output go to ignored `bench/out/`; a passing test or
  visual tweak needs no report folder, README, or archived status entry;
  reusable harnesses live in the owning test/tool directory.
- never embed counts, percentages, or benchmark results in prose; say what
  kind of thing exists and cite the artifact ("See `path/to/artifact.json` for
  current count.")
- structural claims ("Doe has a native DXIL emitter") are checked against file existence, not prose
- `docs/status.md` is the concise status front door; live details go at the top of topical shards `docs/status/*.md` (LOC-capped; split by subdomain before the cap); dated append-only history in `docs/status/archive/*.md` changes only in deliberate archive maintenance
- artifacts, not docs, are the source of truth:
  - theorem count/categories: `pipeline/lean/artifacts/proven-conditions.json`
  - benchmark results: `bench/out/*/dawn-vs-doe.*.json`
  - browser smoke: `browser/chromium/artifacts/*/dawn-vs-doe.browser.playwright-smoke.diagnostic.json`
  - backends: `runtime/zig/src/backend/*/`
  - test status: `zig build test-wgsl` exit code

## Documentation style

- Markdown document titles and section headings use sentence case
- Use the [documentation ownership map](docs/INDEX.md#canonical-strategy):
  consolidate repeated sections into their owner before considering file
  deletion; keep concrete architecture, installation, and contract details and
  admitted evidence. Historical routine runs may leave the working
  tree when their immutable Git history remains linked.

## File size

- `runtime/zig/source-layout.json` owns Zig source size policy
- 800 lines: advisory architecture review signal
- handwritten production files above 1,200 lines need a cohesive-module justification in the architecture manifest
- handwritten production files above 1,500 lines fail the line-limit gate
- generated files use separately declared reproducible generation contracts
- split by named semantic responsibility and group by feature (`pipeline_cache.zig`, not `helpers.zig`), never by arbitrary line count; a split must not scatter a single concern
- Python benchmark and tooling files above 1200 lines need a tracked sharding follow-up (owner, next split target) in the relevant live topical shard

## Constants and thresholds

- inline `0`, `1`, simple indexing, and language-level sentinels when they
  express local mechanics clearly; name domain, ABI, policy, threshold, size,
  retry, and timing values
- centralize thresholds in config or module-level constants; a value used in
  more than one place has a single source of truth
- fallbacks must reference named constants or config getters, never bare literals

## Error handling

- fail fast on invalid inputs, stating what was expected and what was received
- unsupported operations return explicit taxonomy errors, not silent no-ops

## Comments

- comments explain why, not what; do not restate the code
- do not add ad-hoc debug logging; use structured trace output

## No speculative engineering timelines

- Never predict how long engineering work will take ("1-2 weeks", "a quick
  fix"). Describe concrete deltas, dependencies, risks, and validation instead.
- For an already-running process, report measured elapsed time, progress, or a
  grounded ETA only when the process exposes one; otherwise report its phase,
  latest output, and whether it is still progressing.

## Style guides

Read the guide for the surface you edit:

- Zig: [`runtime/zig/STYLE.md`](runtime/zig/STYLE.md)
- JavaScript: [`packages/doe-gpu/STYLE.md`](packages/doe-gpu/STYLE.md)
- JSON/config: [`config/STYLE.md`](config/STYLE.md)
- Python: [`bench/STYLE.md`](bench/STYLE.md)
- Lean: [`pipeline/lean/STYLE.md`](pipeline/lean/STYLE.md)

## Benchmark style

Apply the [performance contract](docs/performance-strategy.md) (comparison
class, work equivalence, timing scope, upload, fairness audit) to raw artifacts
before accepting or reporting any timing result. A failed or suspicious
comparison stays diagnostic until its obligations are established.

## Benchmark front doors

- start from `Start here` and `Choose the benchmark` in `bench/README.md`, not
  scattered config filenames
- prefer `python3 bench/cli.py compare` with promoted profiles; list them with
  `python3 bench/cli.py compare --list-promoted`
- promoted coverage (`config/promoted-compare-catalog.json`) is narrower than
  the `platform x surface x runtimeHost` taxonomy; check it or an explicit
  compare config path before benchmarking. Currently: backend native Doe-vs-Dawn
  and package compare (Node/Bun cold and warm Gemma lanes) on `apple-metal`,
  `amd-vulkan`, and `local-d3d12`; plan compare on `apple-metal` only
- AMD Vulkan Gemma270m package compares are explicit config files, not promoted
  `--surface package` profiles; promoted local D3D12 package profiles still
  need a compatible Windows/D3D12 host for claim evidence

## Completion checklist

Apply only the items for areas the change touches:

- schema or gate change: migration notes and `docs/process.md` expectations match
- behavior change: docs in this repo reflect it
- pipeline/trace/replay-visible change: those outputs match the new behavior
- Dawn-vs-Doe benchmarking change: apples-to-apples methodology is documented and enforced by fail-fast checks
- workload marked claimable: verify required work, timing completeness,
  declared fixed-operation controls or substantiated application-level elimination,
  hardware scope, and the fairness audit for any unusually large speedup
- placeholders, temporary methodology choices, follow-up work, or a changed
  boundary or blocker: record in live status (`docs/status.md` plus the
  relevant topical shard); never archive routine edits or passing checks

## Pick the real fix

- Fix correctness bugs; don't relabel them. Effort or scope framing
  ("non-trivial", "worth its own thread", "later") is no reason for a lesser fix.
- "Mark experimental", "add a TODO", or "reword the misleading comment" does not
  substitute for fixing wrong behavior.
- If scope must be split, list the concrete deltas and ask the user which path
  to take; don't pre-decide a smaller version.
