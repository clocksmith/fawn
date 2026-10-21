# Chromium integration acceptance

This file owns browser integration acceptance. The milestone manifest
[`bench/workflows/browser-milestones.json`](bench/workflows/browser-milestones.json)
owns milestone state; [GOALS.md](../../GOALS.md) selects work and the
`gpuFoundation` hierarchy in
[`config/doe-product-strategy.json`](../../config/doe-product-strategy.json)
records the broader unestablished objective. Chromium hooks and
browser-runtime integration are added only when measured benefits justify their
compatibility and maintenance costs; this file defines acceptance once that
selection is made. Browser replacement cannot inherit native or package
qualification. Fawn's durable workspaces above stock Chromium are governed
separately by the [workspace contract](../../docs/fawn-workspace-contract.md).

## Scope

- Integrate Doe at Chromium's WebGPU implementation seam.
- Preserve browser-owned process, sandbox, API, media, and security behavior.
- Keep fallback explicit and disable it in forced-Doe evidence.
- Bind source, runtime, browser, output, and receipt identity.
- Admit wider GPU paths separately after tracing and proving their benefit;
  page WebGPU evidence cannot establish ownership of them.

## Product target

The Chromium runtime integration is successful when a released Chromium-family
archive runs an unchanged WebGPU application through forced Doe with
independently verified output, real hardware identity, reliable lifecycle
behavior, and a measured benefit that justifies the integration's compatibility
and maintenance costs, with the maintainability acceptance below satisfied.
Browser benchmark rows are supporting evidence, not the
product outcome.

The first promotion target is one exact-oracle application lane. Provider
identity must come from the fail-closed runtime selector and bound runtime
artifacts; `wgpuAdapterGetInfo` must retain the physical adapter identity. A Doe
selection on AMD hardware therefore reports Doe as provider and AMD/Radeon as
hardware, never a synthetic `Doe` GPU vendor.

## Milestones

### M0: contracts

- Runtime selector, fallback, benchmark, claim, responsibility, and release
  contracts exist with schemas and checkers.
- The milestone manifest names required artifacts and checks.

### M1: forced runtime

- Chromium launches in declared `dawn`, `doe`, and governed `auto` modes.
- Forced Doe fails closed.
- Runtime identity and fallback state appear in artifacts.

### M2: correctness and reliability

- Dawn and forced Doe run the same browser workload set.
- Independent output oracles, CTS coverage, crash, hang, recovery, device-loss,
  concurrency, and memory checks pass for the promoted platform.
- Trace and replay identity is complete.

### M3: end-to-end performance

- Both runtimes execute equivalent application work.
- Cold and warm user-visible operations report p50, p95, p99, memory, failures,
  retries, and fallback state.
- A practical winning threshold larger than noise is release-blocking for any
  promoted speed claim.

### M4: published release

- A public HTTPS archive binds the browser, Doe runtime, Dawn fallback, and
  compiler bytes.
- Download, proof-page, gallery, comparison, launch, finalizer, and candidate
  receipts all pass their contracts.
- Installation and launch instructions work on the declared clean system.
- The launch receipt binds a passing clean-install check produced from an
  isolated extraction with no borrowed package members and strict forced-Dawn
  and forced-Doe WebGPU smoke.
- Linux packages include every member required by
  `config/browser-release-platform-policy.json`; compact diagnostic archives do
  not satisfy this criterion.
- Reload, teardown, concurrency, recovery, and memory-growth receipts pass for
  the promoted application lane.

## Compiler obligations

- Run Doe and Tint over the same versioned WGSL corpus.
- Preserve source, IR, backend-output, validator, and compiler identity.
- Close semantic, diagnostic, robustness, and backend-emission gaps exposed by
  real browser and downstream-project shaders.
- Publish only phase comparisons with equivalent boundaries and enough samples.
- Link CTS shader evidence to the same source/output identity chain.

## Native runtime obligations

- Require independent output oracles for promoted compute and render rows.
- Match upload, command, submit, completion, and readback work against Dawn.
- Cover crash, hang, device loss, recovery, concurrency, teardown, and memory.
- Preserve typed errors through C, Zig, browser, and JavaScript boundaries.
- Keep cache, synchronization, fallback, and hardware shortcuts explicit.
- Produce backend-specific Metal, Vulkan, and D3D12 evidence before broadening
  platform claims.

## Chromium seam obligations

- Trace an ordinary webpage from device creation through shader/resource
  creation, submission, completion, readback, canvas presentation and destruction
  under Doe. Disabling Doe must make that page operation fail.
- Compare stock Chromium+Dawn and forced-Doe Chromium from the same pinned
  source and build configuration except for the declared integration; preserve
  ordinary application code and exclude Fawn workspace changes from attribution.
- Maintain explicit `dawn`, `doe`, and governed `auto` selection.
- Make forced `doe` fail closed when Doe cannot initialize.
- Record browser executable, Doe runtime, Dawn fallback, compiler, adapter,
  driver, selector, and fallback identity in every promoted artifact.
- Preserve Chromium process, sandbox, renderer, media, accessibility, and
  security boundaries outside the WebGPU implementation seam.
- Validate canvas, presentation, external texture, media, worker, recovery,
  and device-loss behavior under forced Doe.
- Run browser CTS and application workloads under both forced runtimes.

## Maintainability acceptance

Maintainability is required for accepting Doe as Chromium's GPU foundation,
alongside correctness and application performance. Collect evidence during
integration and revalidate it on each claimed upstream-version transition.
Before qualification, freeze the Chromium version sequence, supported host,
patch scope, duplication baseline, commands, repetitions and numeric cost
bounds in a versioned campaign contract. Missing bounds or evidence fail
acceptance; qualitative descriptions such as "small" or "easy to rebase" do not
pass. Existing milestone receipts do not establish these new requirements.

| Requirement | Required evidence and acceptance |
| --- | --- |
| Narrow Chromium patch set | An ordered patch series bound to upstream revisions, touched components and diff size, with the reason and owner for each seam. It stays within the frozen scope and size bounds; unexplained unrelated browser changes fail. |
| Reproducible builds | Independent clean builds from pinned source, dependencies, toolchains, arguments and patches. Retain commands, logs and artifact hashes. Declare any unavoidable nondeterministic metadata before building and verify normalized artifact equivalence without ignoring executable differences. Clean installation and the same functional gates pass for each build. |
| Chromium-version rebases | A predeclared sequence of upstream version transitions, with retained conflicts, resolutions and patch changes. Every transition builds and passes page ownership, correctness, lifecycle and required performance regressions; a clean patch application alone does not pass. Retain failed attempts. |
| Reduced duplicated backend code | A frozen inventory of duplicated responsibilities across Doe and Chromium adapters, source-bound before/after implementations and measured reduction. Name the remaining owner of each responsibility and retain behavior/regression evidence. Moving copies, dropping supported behavior or adding a shared layer without removing duplication does not pass. |
| Bounded build/test cost | Clean and incremental build wall time, CPU, peak RSS and disk, plus focused development and qualification test costs, on the pinned host with declared cache state and repetitions. Both absolute budgets and regression limits must pass; reducing coverage to meet a bound fails. |
| Readable ownership and diagnostics | A reviewed map from webpage API through integration, compiler, runtime and backend, naming lifetime and error ownership. Retained fault reproductions expose the failing boundary, provider, source/build identity and available source location. Unattributed faults remain explicit; hidden fallback, duplicate ownership or invented locations fail. |

Qualification tooling must bind these receipts to the exact candidate and
enforce the frozen bounds before a foundation claim. The strategy schema records
required evidence and rejects a premature established assessment; it is not a
physical build, rebase, cost or ownership validator. Until those validators and
receipts exist, maintainability remains unestablished.

## Upstream tracking and repair

Fawn owns the browser-replacement implementation in this repository's browser
tree; Doe owns the WebGPU implementation/compiler and its integration seams.
Private strategy is not a runtime dependency or evidence of implementation.
Use the existing [upstream intelligence](../../pipeline/upstream_intelligence/README.md)
for bounded Dawn/Tint and linked Chromium issue intake. It preserves observations
and review proposals; it is not an autonomous Chromium source repair engine.
The [pinned patch applicator](scripts/apply-runtime-patches.sh) checks revisions
and conflicts before applying changes; application alone proves no repair.

Freeze the modern OS/GPU/API envelope from the [support matrix](../../docs/doe-support-matrix.md),
upstream transition set, workload population and numeric acceptance bounds before
historical replay or live qualification. A repair candidate must:

1. Bind detected upstream changes and unavailable/deferred coverage to exact
   revisions, hashes and the configured intake bounds.
2. Retain a reproducible original failure and independent expected behavior;
   minimize the reproduction without deleting the failing behavior.
3. Propose a bounded patch at the existing owner, retaining rejected proposals.
   No untrusted upstream text, model output or review receipt may mutate runtime
   policy or the production browser by itself.
4. Pass unchanged build, output, lifecycle, security and matched Dawn comparison
   gates for the declared envelope. Deleting failing tests, weakening validation
   or sandboxing, and silently shrinking support or workloads invalidate repair.
5. Demonstrate rollback to the pinned prior browser/runtime and retain source,
   binary, command, failure and qualification receipts for both outcomes.

Use the existing offline contract tests and isolated historical replay commands
in the intake README before any live repair loop. Required repair measurements
are detection-to-reproduction and reproduction-to-verified-repair latency,
accepted repairs / attempted transitions, false successes / claimed successes,
human intervention count and reason, and patch files/lines/seams. Record failures
and the full denominator; an intake backlog or successful patch application is
not a successful repair. Freeze thresholds with the maintainability campaign;
missing bounds, rollback evidence or any known false success block promotion.

These are acceptance obligations, not a claim of an implemented reliable
self-healer. Existing intake tests do not validate browser patch synthesis,
physical rebase qualification or repair metrics. Implement those missing checks
at their existing owners before describing the loop as reliable.

## Published-browser obligations

- Produce a downloadable, hash-bound Chromium-family archive.
- Bind the browser binary, Doe runtime, Dawn fallback, and compiler artifacts.
- Publish a successful HTTPS download receipt.
- Provide a proof page that exposes active runtime, fallback state, and recent
  receipt links.
- Provide hosted application pages and same-workload Dawn/Doe comparison
  receipts.
- Pass the published-release, proof-surface, launch, finalizer, and
  release-candidate contracts under `browser/chromium/contracts/`.

Until these obligations pass, browser evidence remains diagnostic.

## Promotion rule

Milestones are cumulative. Missing M2 evidence cannot be offset by M3 timing,
and local M3 evidence cannot substitute for M4 distribution proof.

Until M4 passes for a declared platform, the browser lane remains diagnostic.

## Archived work

Track B internal-module proposals are retired. Historical contracts may remain
for reference, but they are not active milestones or product surfaces.
