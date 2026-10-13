# Chromium integration acceptance

This file owns browser integration acceptance. The milestone manifest
[`bench/workflows/browser-milestones.json`](bench/workflows/browser-milestones.json)
owns state; [GOALS.md](../../GOALS.md) selects work. Chromium hooks and
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

## Product target

The Chromium runtime integration is successful when a released Chromium-family
archive runs an unchanged WebGPU application through forced Doe with
independently verified output, real hardware identity, reliable lifecycle
behavior, and a measured benefit that justifies the integration's compatibility
and maintenance costs. Browser benchmark rows are supporting evidence, not the
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

- Maintain explicit `dawn`, `doe`, and governed `auto` selection.
- Make forced `doe` fail closed when Doe cannot initialize.
- Record browser executable, Doe runtime, Dawn fallback, compiler, adapter,
  driver, selector, and fallback identity in every promoted artifact.
- Preserve Chromium process, sandbox, renderer, media, accessibility, and
  security boundaries outside the WebGPU implementation seam.
- Validate canvas, presentation, external texture, media, worker, recovery,
  and device-loss behavior under forced Doe.
- Run browser CTS and application workloads under both forced runtimes.

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
