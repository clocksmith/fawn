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

- The shared service, its Playwright and raw-CDP adapters, the research,
  form-submission and export workflow adapters, and the qualification and
  baseline-comparison harness in this directory.

## Contracts

Inputs:
- [Workspace contract](../../docs/fawn-workspace-contract.md).
- [Ordered workspace roadmap](../../config/doe-product-strategy.json).
- Frozen [behavioral case set](../../config/fawn-workspace-qualification.json) and
  [comparison scenarios and criteria](../../config/browser-product-comparison-policy.json).

Outputs:
- Durable authority/action records and versioned observations.
- Workflow, fault-injection and baseline evidence, admitted only by the validator.

## Invariants

- All Fawn automated mutations pass the trusted dispatcher.
- Owner generations persist; every adapter rejects stale commands identically.
- Completed, failed and unknown outcomes survive restart and handoff; missing
  acknowledgment never authorizes another external submission.
- Research, form-submission and export adapters behave consistently.
- Workspace acceptance is separate from browser/GPU recovery and artwork receipts.

## Acceptance

- Pass the workspace contract's behavioral gates and competent-baseline comparison.
- Evidence: a full [qualification](README.md#qualification) result admitted by
  `qualification/validate.js` for committed source, and a frozen-criteria comparison.
  [Development](test/run-tests.js) and [policy tests](../../bench/tests/test_fawn_workspace_strategy.py)
  replace neither.

## Non-goals

- Workflow-specific planning, implicit resubmission or atomic external effects.

## Freedom

Any implementation must preserve these boundaries and earn independent evidence.
