# Doe documentation index

Use this page to find the owner of a subject. Live measurements and mutable
state belong in artifacts, schemas, and status front doors—not copied prose.

The root [`README.md`](../README.md) is the project orientation and first
entry point. This index is for finding detail after the project shape and
evidence boundaries are clear.

## Canonical strategy

- [`../GOALS.md`](../GOALS.md): product goals, constraints, and exclusions
- [`../INTENT.md`](../INTENT.md): enforceable system invariants
- [`../CATSCAN.md`](../CATSCAN.md): root component ownership and acceptance boundaries
- [`../AGENTS.md`](../AGENTS.md): contributor instructions and canonical-policy discovery
- [`component-index.md`](component-index.md): generated recursive component map
- [`component-charters.md`](component-charters.md): charter placement,
  precedence, required shape, validation, and handoff protocol
- [`thesis.md`](thesis.md): technical rationale and the three proofs that select
  current work
- [`product-strategy-contract.md`](product-strategy-contract.md): the technical
  strategy contract, its schema migrations, and validation

Keep each governing rule at its owner and link it from other front doors.
Architecture, installation guides, and contracts retain concrete implementation
and usage details. Reports retain evidence and rejected experiments; consolidation
of repeated prose does not delete them.
- Cross-project documentation authority, the commercial plan, and the ordered
  roadmap are maintained in the owner's private strategy repository; Doe remains
  canonical for Doe behavior and measurements

## Product and project contracts

- [`problems-addressed.md`](problems-addressed.md): user problems and proof required
- [`runtime-ownership-decision.md`](runtime-ownership-decision.md): four-lane
  DoeProof-versus-DoeRuntime attribution, operational trust, ownership cost,
  application tiers, and expansion admission
- [`architecture.md`](architecture.md#technical-diagrams): compiler/runtime ownership, native submission, prepared-resource lifetimes, and system surfaces
- [`implementation-peers.md`](implementation-peers.md): generated implementation
  inventory, compiler identities, provider IDs and tracking pointers
- [`process.md`](process.md): stage, gate, and release law
- [`status.md`](status.md): concise current-status routing
- [`repo-taxonomy.md`](repo-taxonomy.md): repository ownership
- [`glossary.md`](glossary.md): product names and qualified legacy terms
- [`licensing.md`](licensing.md): licensing and third-party boundaries
- [`upgrade-policy.md`](upgrade-policy.md): compatibility and migrations

## Package, support, and claims

- [`node-bun-developer-wedge.md`](node-bun-developer-wedge.md): package and
  downstream promotion contract; not a separate strategy
- [`doe-support-matrix.md`](doe-support-matrix.md)
- [`package-model.md`](package-model.md)
- [`doe-gpu-node-runtime-scope.md`](doe-gpu-node-runtime-scope.md)
- [`runtime-surface-boundary.md`](runtime-surface-boundary.md)
- [`internal-tooling.md`](internal-tooling.md)
- [`public-claim-boundary.md`](public-claim-boundary.md)
- [`backend-evidence-matrix.md`](backend-evidence-matrix.md)
- [`config-schema-enforcement.md`](config-schema-enforcement.md)
- [`ecosystem.md`](ecosystem.md): actor research, scoring, adoption evidence,
  downstream release promotion, and evidence routing

## Workloads, performance, and operations

- [`workload-system.md`](workload-system.md)
- [`benchmark-taxonomy.md`](benchmark-taxonomy.md)
- [`performance-strategy.md`](performance-strategy.md): measurement and
  comparison contract; not a separate strategy
- [`operator-runbook.md`](operator-runbook.md)
- [`concurrency-strategy.md`](concurrency-strategy.md)
- [`numeric-stability.md`](numeric-stability.md)
- [`metal-macos-proof-bundle-runbook.md`](metal-macos-proof-bundle-runbook.md)
- [`apple-metal-runtime-release.md`](apple-metal-runtime-release.md)

## Compiler and TSIR

- [`shader-compiler-architecture.md`](shader-compiler-architecture.md)
- [`tsir-contract.md`](tsir-contract.md)
- [`lean-bounds-elimination-design.md`](lean-bounds-elimination-design.md)
- [`doppler-ingest.md`](doppler-ingest.md)
- [`gpu-math-api-proposal.md`](gpu-math-api-proposal.md)

## Browser

- [`fawn-workspace-contract.md`](fawn-workspace-contract.md): Fawn's durable
  browser workspaces — authority, action recovery, reusable workflows and
  prospective acceptance
- [`status/fawn-workspace.md`](status/fawn-workspace.md): missing implementation
  and acceptance boundary, independent of artwork and runtime integration
- [`browser-lane.md`](browser-lane.md): optional Chromium runtime integration,
  retained A/B/C/D attribution, and the separate Cloudflare Kitesurf K0
  external-product comparator
- [`fawn-fork-maintenance-policy.md`](fawn-fork-maintenance-policy.md):
  conditional Chromium fork maintenance cost, applicable only after a
  measured-benefit selection

## Cerebras and CSL

Not in the current selection; these pages describe the lane. See [`cerebras.md`](cerebras.md) for the admission rule.

- [`cerebras.md`](cerebras.md): lane front door
- [`csl-architecture.md`](csl-architecture.md)
- [`csl-quickstart.md`](csl-quickstart.md)
- [`csl-layer-block-self-check.md`](csl-layer-block-self-check.md)
- [`cerebras-hardware-runbook.md`](cerebras-hardware-runbook.md)
- [`cerebras-model-ledgers.md`](cerebras-model-ledgers.md)
- [`claim-discipline.md`](claim-discipline.md)
- [`cerebras-evidence-bundle.md`](cerebras-evidence-bundle.md)
- [`cerebras-evidence-bundle-pointer.md`](cerebras-evidence-bundle-pointer.md)
- [`hardware-validation-appendix.md`](hardware-validation-appendix.md)

Formal browser contracts, generated bundle sources, and status archives retain
their directory-local indexes and should not be duplicated here.
