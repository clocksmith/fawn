# CATSCAN: Doe

Parent: none

## Target
Keep Fawn workspace, compiler, runtime, package, browser, and qualification components independently
owned and accepted under the repository's goals and system invariants.

## Authority
- Owns component boundaries and acceptance routing.
- Compiler owns shader meaning; runtime owns execution and lifetime;
  qualification owns impartial evidence, not application execution policy.
- Fawn workspace owns browser-work authority above Chromium; workflow adapters
  own task logic. Shared GPU components exclude browser policy and model semantics.
- Excludes external application planning, model selection, and external
  hardware governance.

## Scope
- Includes this tree; child charters narrow authority.

## Contracts
Inputs:
- Goals and exclusions: [GOALS.md](GOALS.md).
- System invariants: [INTENT.md](INTENT.md).
- Technical rationale and current proofs: [thesis](docs/thesis.md).
- Stage and gate law: [process](docs/process.md).
Outputs:
- Workspace records, package, runtime, compiler, and qualification artifacts.
- [Component authority index](docs/component-index.md).

## Invariants
- Every component inherits [system invariants](INTENT.md) and its parent charter.
- Children cannot broaden authority, weaken invariants, or transfer evidence
  between independently admitted surfaces.
- Boundary changes require affected charters and acceptance evidence to change
  together; existing code or failed receipts cannot redefine authority.

## Acceptance
- Charters and their generated index pass the blocking CATSCAN gate.
- Evidence: [CATSCAN gate](bench/gates/catscan_gate.py).
- Each component supplies behavioral evidence for its contracts under
  [INTENT.md](INTENT.md); charter structure alone does not qualify behavior.

## Non-goals

- Product strategy, application ownership, or promotion outside declared boundaries.

## Freedom

Implementation freedom requires these boundaries and acceptance evidence.
