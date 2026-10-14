# CATSCAN: Fawn workspace

Parent: [Browser](../CATSCAN.md)

## Target

Provide durable browser workspaces for agents through one shared observation,
action and recovery service above stock Chromium.

## Authority

- Owns workspace identity, authorization, observation versions, owner generations,
  trusted automated action dispatch and uncertain-outcome recovery.
- Workflow adapters own task logic; Chromium owns browser semantics and security.
- Does not own model semantics, GPU qualification or external transaction guarantees.

## Scope

- The shared service, its narrow Playwright and raw-CDP adapters, and the
  research, form-submission and export workflow adapters in this directory.
  Implementation existence and development tests establish no runtime acceptance.

## Contracts

Inputs:
- [Workspace contract](../../docs/fawn-workspace-contract.md).
- [Ordered workspace roadmap](../../config/doe-product-strategy.json).

Outputs:
- Durable authority/action records and versioned observations after implementation.
- Independent reusable-workflow, fault-injection and baseline evidence.

## Invariants

- All Fawn automated mutations pass the trusted dispatcher.
- Owner generations persist; every adapter rejects stale commands identically.
- Completed, failed and unknown outcomes survive restart and handoff; missing
  acknowledgment never authorizes another external submission.
- Research, form-submission and export adapters behave consistently.
- Workspace acceptance is separate from browser/GPU recovery and artwork receipts.

## Acceptance

- Pass the workspace contract's behavioral gates and competent-baseline comparison.
- Evidence: [development tests](test/run-tests.js) (`npm test`, `npm run test:browser`;
  see the [README](README.md)) and [policy tests](../../bench/tests/test_fawn_workspace_strategy.py)
  that keep the strategy projection unqualified. Neither replaces the contract's
  frozen behavioral and baseline evidence.

## Non-goals

- Workflow-specific planning, implicit resubmission or atomic external effects.

## Freedom

Any implementation must preserve these boundaries and earn independent evidence.
