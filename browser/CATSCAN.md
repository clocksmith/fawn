# CATSCAN: Browser

Parent: [Doe](../CATSCAN.md)

## Target

Govern Fawn durable browser work and optional browser-hosted Doe execution as
separately admitted surfaces independent of package and native claims.

## Authority

- Owns workspace authority above Chromium and optional runtime integration boundaries.
- Does not own Chromium-wide policy, package wrappers, native runtime semantics, or browser claims without a released artifact.

## Scope

- Includes files beneath this directory except child-chartered components, which narrow this authority.

## Contracts

Inputs:
- Browser boundary: [`../docs/browser-lane.md`](../docs/browser-lane.md).
- Runtime surface law: [`../docs/runtime-surface-boundary.md`](../docs/runtime-surface-boundary.md).
- Workspace contract: [`../docs/fawn-workspace-contract.md`](../docs/fawn-workspace-contract.md).

Outputs:
- Governed browser integration components and separately classified browser evidence.

## Invariants

- Browser evidence never inherits package or native claim status.
- The incumbent browser wrapper is not represented as Doe beneath `navigator.gpu`.
- Forced-provider identity and fallback state remain explicit.
- Workspace authority/recovery acceptance never inherits GPU/browser receipts.

## Acceptance

- Workspace work routes through its shared-service contract; runtime substitution
  routes through Chromium acceptance and release contracts.
- Evidence: [`chromium/acceptance.md`](chromium/acceptance.md).

## Non-goals

- Uncontrolled automation outside the workspace contract, Chromium-wide replacement,
  or generic browser benchmark claims.

## Freedom

Any mechanism is permitted if it preserves these boundaries and passes the acceptance evidence.
