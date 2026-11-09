# CATSCAN: Chromium runtime integration

Parent: [Browser](../CATSCAN.md)

## Target

Own Fawn page WebGPU; require correctness, benefit and maintainability.

## Authority

- Owns Chromium contracts, runtime selection, packaging, lifecycle probes, release evidence and Linux GPU policy, including denied-target scheduling.
- Wider integration requires traced paths, measured selection and separate acceptance; no inherited evidence or unrelated policy.

## Scope

- This directory, narrowed by child charters.

## Contracts

Inputs:
- Acceptance: [`acceptance.md`](acceptance.md).
- Browser contracts: [`contracts/README.md`](contracts/README.md).

Outputs:
- Hash-bound archives, forced-provider receipts, application results, diagnostics, release decisions.

## Invariants

- Release candidates pass isolated clean installation using only packaged inputs.
- Prepare immutable runtime closures; never overwrite mapped libraries.
- Forced Doe fails closed; governed fallback remains explicit and typed.
- Windows C dependencies stay in private `lib/doe`, separate from browser/Dawn;
  launch preserves caller working directory.
- Launch proves neither application nor performance value.
- Layered cohorts stop after unknown completion; visual owners stop and drain after measurement.
- Hosted artwork keeps input-independent visible-time scheduling; input keeps each
  scene's weighted response through transitions without delaying handoffs or resetting material.
- Logo recovery lerps live parcels into a shared boundary before its dwell, and
  release retraces them across the scene; checks follow positions, not opacity.
- Branded silhouettes retain live fields and parcel advection. Unformed fluids
  shade live fields; transitions interpolate carried light.

## Acceptance

- Machine-owned milestone gates pass.
- Foundation acceptance requires [maintainability](acceptance.md#maintainability-acceptance); historical milestones cannot discharge it.
- Evidence: [`bench/workflows/browser-milestones.json`](bench/workflows/browser-milestones.json).

## Non-goals

- General Chromium ownership, orchestration or smoke-based promotion.

## Freedom

Preserve boundaries and acceptance.
