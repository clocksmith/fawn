# CATSCAN: Workspace product client

Parent: [Fawn workspace](../../CATSCAN.md)

## Target

Delegate bounded investigations and resume from saved evidence with human control.

## Authority

- Owns human sessions, bounded planning and investigation artifacts.
- Consumes service authority, dispatch and reconciliation; cannot replace them.
- Excludes model semantics, GPU qualification and product-advantage verdicts.

## Scope

- Loopback client, bounded planning, repository evidence, investigation records and opt-in workflow reuse.

## Contracts

Inputs:
- [Protocol](../../../../docs/fawn-workspace-contract.md).
- [Configuration](../../../../config/fawn-workspace-client.json).

Outputs:
- Human controls, explicit delegation and source-bound reasoning revisions.

## Invariants

- Only the service dispatches browser effects.
- Page/region/action grants and saved-record disclosure are explicit; delegations inherit no prior grants.
- Restart begins with human ownership and no model calls; pending tools are never replayed.
- Takeover invalidates future work, not dispatched effects. Uncertainty never triggers retries.
- Model output grants no permission. Repository reads stay under the selected root, including symlinks.
- Corrections invalidate dependents without mutating earlier revisions.
- Credentials stay private; HTTP exposes no browser handle or service credential.
- Trace replay preserves service admission and stops before submission.

## Acceptance

- Evidence: [Client tests](../../test/unit/product-client.test.js): interruption, HTTP authority and restart.
- [Investigation tests](../../test/unit/investigation.test.js): revisions and source containment.
- [Workflow tests](../../test/unit/workflow-agent.test.js): verified learning, slot-bound reuse and safe fallback.
- [Browser journey](../../test/browser/product-surface.test.js): UI, uncertainty and resumption with a controlled planner.
- Parent comparison remains mandatory; controlled planners prove no live-model quality or product advantage.

## Non-goals

- Automatic approval of conclusions or repackaged qualification agents.

## Freedom

Preserve boundaries and independent evidence.
