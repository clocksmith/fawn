# CATSCAN: Workspace product client

Parent: [Fawn workspace](../../CATSCAN.md)

## Target

Human-controlled investigations and workflow reuse.

## Authority

- Owns client planning, investigations and workflow persistence.
- Consumes service authority, dispatch and reconciliation.
- Excludes model semantics, GPU qualification and product-advantage verdicts.

## Scope

- Loopback client, investigations, workflow reuse and sealed registry.

## Contracts

Inputs:
- [Protocol](../../../../docs/fawn-workspace-contract.md).
- [Configuration](../../../../config/fawn-workspace-client.json).

Outputs:
- Explicit delegation and source-bound revisions.

## Invariants

- Only the service dispatches browser effects.
- Grants and record disclosure are explicit; delegations inherit no grants.
- Restart restores human ownership; no model calls or tool replay.
- Takeover stops future work. Dispatched effects persist. Uncertainty forbids retries.
- Models grant no permission. Repository reads, including symlinks, stay under the selected root.
- Corrections invalidate dependents without mutating earlier revisions.
- HTTP exposes no browser handles or service credentials.
- Replay checks accounts/disclosure, preserves service admission and stops before submission.
- Scoped reuse requires reviewed meanings, unique targets and ordered dependency boundaries.
- Shared graphs are encrypted on clients; remote storage grants no browser authority.
- Publication locks across processes; independent attempt records survive publication failure.

## Acceptance

- Evidence: [Client tests](../../test/unit/product-client.test.js): authority/restart.
- [Persistence checks](../../workflow-store.md#focused-checks): isolation, attempt accounting and crash preservation.
- [Sync checks](../../workflow-registry.md#focused-evidence): encrypted cross-client reuse.
- [Scoped replay](../../test/unit/scoped-trace.test.js): aliases, dependencies and safe stops.
- [Holdout decision](../../../../reports/fawn-workspace/20261109-field-group-reuse/decision.json): bounded reuse, no product verdict.
- Parent comparison remains mandatory; controlled planners prove no product advantage.

## Non-goals

- Automatic approval of conclusions or repackaged qualification agents.

## Freedom

Preserve boundaries and independent evidence.
