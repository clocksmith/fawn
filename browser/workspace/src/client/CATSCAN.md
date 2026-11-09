# CATSCAN: Workspace product client

Parent: [Fawn workspace](../../CATSCAN.md)

## Target

Investigations and workflow reuse under human control.

## Authority

- Owns sessions, planning, investigation records and personal workflows.
- Consumes service authority, dispatch and reconciliation.
- Excludes model semantics, GPU qualification and product-advantage verdicts.

## Scope

- Loopback client, repository evidence, investigations and opt-in workflow reuse.

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
- Trace replay preserves service admission and stops before submission.
- Verified workflows publish under a cross-process lock; account/disclosure checks precede effects.

## Acceptance

- Evidence: [Client tests](../../test/unit/product-client.test.js): interruption, HTTP authority and restart.
- [Investigation tests](../../test/unit/investigation.test.js): revisions and source containment.
- [Workflow tests](../../test/unit/workflow-agent.test.js): learning, reuse and fallback.
- [Persistence tests](../../test/unit/workflow-store.test.js) and
  [browser test](../../test/browser/workflow-store.test.js): isolation, snapshots, concurrent/crashed publication.
- [Browser journey](../../test/browser/product-surface.test.js): UI, uncertainty and resumption with a controlled planner.
- Parent comparison remains mandatory; controlled planners prove no product advantage.

## Non-goals

- Automatic approval of conclusions or repackaged qualification agents.

## Freedom

Preserve boundaries and independent evidence.
