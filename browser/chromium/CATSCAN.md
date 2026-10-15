# CATSCAN: Chromium runtime integration

Parent: [Browser](../CATSCAN.md)

## Target

Own page WebGPU for Doe's Chromium GPU foundation; require correctness, benefit and maintainability.

## Authority

- Owns Chromium integration contracts, runtime selection, artifact packaging, browser lifecycle probes, and release evidence.
- Wider GPU integration requires traced paths, measured selection and separate acceptance; excludes unrelated Chromium policy and inherited package evidence.

## Scope

- Includes this directory, subject to child charters.

## Contracts

Inputs:
- Acceptance: [`acceptance.md`](acceptance.md).
- Browser contracts: [`contracts/README.md`](contracts/README.md).

Outputs:
- Hash-bound browser archives, forced-provider receipts, application results, diagnostics, and release decisions.

## Invariants

- Release candidates pass isolated clean installation using only packaged inputs.
- Forced Doe fails closed; governed fallback remains explicit and typed.
- Browser launch alone is integration evidence, not application or performance value.
- Hosted artwork keeps input-independent visible-time scheduling; input keeps each
  scene's weighted response through transitions without delaying handoffs or resetting material.
- Logo recovery lerps live parcels into a shared boundary before its dwell, and
  release retraces them across the scene; checks follow positions, not opacity.
- Branded silhouettes retain live fields and parcel advection. Unformed fluids
  shade their live fields; fluid transitions interpolate carried light.

## Acceptance

- The machine-owned milestone manifest defines and passes the applicable browser gates.
- Foundation acceptance requires [maintainability](acceptance.md#maintainability-acceptance); historical milestones cannot discharge it.
- Evidence: [`bench/workflows/browser-milestones.json`](bench/workflows/browser-milestones.json).

## Non-goals

- General Chromium ownership, browser-wide agent orchestration, or claim promotion from local smoke tests.

## Freedom

Any mechanism is permitted if it preserves these boundaries and passes the acceptance evidence.
