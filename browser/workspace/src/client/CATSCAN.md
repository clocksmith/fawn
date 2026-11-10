# CATSCAN: Workspace product client

Parent: [Fawn workspace](../../CATSCAN.md)

## Target

Controlled investigations and reusable workflows.

## Authority

- Owns planning, investigations and workflow persistence.
- Consumes dispatch/reconciliation.
- Excludes model/GPU qualification and product verdicts.

## Scope

- Loopback client, reusable workflows and sealed registry.

## Contracts

Inputs:
- [Protocol](../../../../docs/fawn-workspace-contract.md).
- [Configuration](../../../../config/fawn-workspace-client.json).

Outputs:
- Explicit delegation and source-bound revisions.

## Invariants

- Service dispatches browser effects exclusively.
- Default: pinned Fawn. Explicit stock Chromium selection; no fallback.
- Delegation inherits no grants or record disclosure.
- Restart restores human ownership without replay.
- Takeover stops future work. Dispatched effects persist. Uncertainty forbids retries.
- Models grant no permission; repository reads/symlinks stay under selected roots.
- Corrections invalidate dependents; earlier revisions persist.
- HTTP exposes no handles/credentials.
- Replay checks accounts/disclosure, preserves service admission and stops before submission.
- Scoped reuse requires reviewed meanings, unique targets and ordered dependency boundaries.
- Verified repairs retain the executed prefix; exact-page suffixes require independent review.
- Client-encrypted graphs; remote storage grants no browser authority.
- Publication preserves independent attempt records.

## Acceptance

- Evidence: [Client tests](../../test/unit/product-client.test.js): authority/restart; [shopping](../../test/unit/shopping.test.js): approval/recovery.
- [Persistence checks](../../workflow-store.md#focused-checks): isolation, attempt accounting and crash preservation.
- [Sync checks](../../workflow-registry.md#focused-evidence): encrypted cross-client reuse.
- [Scoped replay](../../test/unit/scoped-trace.test.js): aliases, dependencies and safe stops.
- [Repair replay](../../test/browser/workflow-repair.test.js): separate-process persistence.
- [Holdout decision](../../../../reports/fawn-workspace/20261109-field-group-reuse/decision.json): bounded reuse.
- Parent comparison remains mandatory.

## Non-goals

- Automatic approval of conclusions or repackaged qualification agents.

## Freedom

Preserve boundaries and independent evidence.
