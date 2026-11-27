# CATSCAN: Blitzen product client

Parent: [Blitzen workspace](../../CATSCAN.md)

## Target

Investigations and reusable workflows.

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

- The service owns dispatch. Stock Chrome is the default. Fawn requires a hash-pinned profile.
- Delegation grants no permissions or disclosure. Restart restores human control. Takeover stops future work. Uncertain effects forbid retries.
- Repository reads stay within selected roots, through symlinks. Corrections invalidate dependent revisions and preserve history.
- Required records must be current and task-visible before completion. Counts establish presence only.
- HTTP keeps handles and credentials private. Replay checks accounts, disclosure and admission, stopping before submission.
- DAGs merge matching outlines and slots, compress detours and stop on semantic ties. Scoped reuse requires reviewed meanings, unique targets and ordered dependencies.
- Repairs retain executed prefixes. Exact-page suffixes require independent review. Graphs use client encryption. Publication preserves each attempt.

## Acceptance

- Evidence: [client authority](../../test/unit/product-client.test.js), [shopping recovery](../../test/unit/shopping.test.js), [DAG routing](../../test/unit/trace-graph-dp.test.js), [completion](../../test/unit/completion-contract.test.js).
- [Persistence](../../workflow-store.md#focused-checks), [encrypted sync](../../workflow-registry.md#focused-evidence), [scoped replay](../../test/unit/scoped-trace.test.js), [repair](../../test/browser/workflow-repair.test.js) and the [bounded holdout](../../../../reports/fawn-workspace/20261109-field-group-reuse/decision.json) cover reuse. Parent comparison required.

## Non-goals

- Automatic approval of conclusions or repackaged qualification agents.

## Freedom

Preserve independent evidence.
