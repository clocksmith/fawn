# CATSCAN: Fawn workspace

Parent: [Browser](../CATSCAN.md)

## Target

Provide durable agent workspaces through shared observation, action and recovery
above stock Chromium.

## Authority

- Owns workspace identity, authorization, versioned observations, owner generations,
  trusted dispatch and uncertain-outcome recovery.
- Adapters own task logic; Chromium owns browser semantics/security.
- Excludes model semantics, GPU qualification and external transaction guarantees.

## Scope

- Shared service, Playwright/raw-CDP adapters, research/form-submission/export
  workflows, qualification and baseline-comparison harness.

## Contracts

Inputs:
- [Workspace contract](../../docs/fawn-workspace-contract.md).
- [Workspace acceptance bindings](../../config/doe-product-strategy.json).
- Frozen [behavioral case set](../../config/fawn-workspace-qualification.json) and
  [comparison scenarios and criteria](../../config/browser-product-comparison-policy.json).

Outputs:
- Durable authority/action records and versioned observations.
- Validator-admitted workflow, fault-injection and baseline evidence.

## Invariants

- Automated mutations pass trusted dispatch.
- Persist owner generations; adapters reject stale commands identically.
- Completed/failed/unknown outcomes survive restart/handoff; missing acknowledgment
  never authorizes resubmission.
- Research/form-submission/export behave consistently.
- Workspace acceptance excludes browser/GPU recovery and artwork receipts.
- Before launch, explicit build selection binds image/private runtime closure;
  both adapters force selected C/Dawn without fallback. Launch identity proves
  no GPU ownership.
- Treatment/control share requested launch configuration; image mismatches prove
  no comparison advantage.

## Acceptance

- Pass contract behavioral gates and competent-baseline comparison.
- Evidence: a full [qualification](README.md#qualification) result admitted by
  `qualification/validate.js` for committed source, and a frozen-criteria comparison.
  [Development](test/run-tests.js) and [policy tests](../../bench/tests/test_fawn_workspace_strategy.py)
  replace neither.

## Non-goals

- Workflow-specific planning, implicit resubmission or atomic external effects.

## Freedom

Preserve boundaries and earn independent evidence.
