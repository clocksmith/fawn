# Fawn durable workspace contract

Status: **shared service implemented in [`browser/workspace`](../browser/workspace/README.md);
behavioral gates frozen; see [status](status/fawn-workspace.md) for admitted evidence**.
This contract owns prospective shared-service behavior and acceptance.
[Goals](../GOALS.md) own product intent, [the thesis](thesis.md) owns rationale,
and `fawnWorkspace` in [the strategy contract](../config/doe-product-strategy.json)
binds the steps' current assessments to admitted evidence. Do not add a roadmap
here.

## Service and workflow boundaries

An explicitly selected Fawn browser build must use the same trusted dispatcher
and private-pipe adapters as installed Chromium. Service schema 4 binds an actual
browser executable, declared dependency files and the selected C runtime by
SHA-256 before each launch. Missing or changed files fail without fallback;
Dawn rollback is explicit. Qualification retains that profile alongside the
unchanged service rules and checks the observed executable. This launch contract
does not establish Doe GPU ownership: page execution, presentation, lifetime and
missing-C controls remain the Chromium integration's acceptance boundary.

Fawn provides durable browser workspaces in the environment Doe builds for
agents. Begin with one shared observation, action, and recovery service above
stock Chromium, with narrow CDP/Playwright adapters. Persist workspace identity,
authorization, ownership generation, observation versions and action records.
Workflow adapters implement task-specific logic using the same machinery.

The service is an authority boundary, not another unconstrained automation
client. All Fawn-controlled automated browser mutations pass through its trusted
dispatcher. Human browser activity and unrelated external actors remain possible
and must be accounted for by observation and preconditions. Direct CDP access
cannot be an undocumented route around Fawn's dispatch checks. Define adapter
credentials, caller authentication and mutation coverage before runtime admission.

File disclosure requires an administrator's explicit selected bytes and site/page
grant. Agents receive file identifiers, never arbitrary local read paths. Upload
dispatch checks those grants and immutable snapshots again, including revocation
while queued. Attachment and application acceptance are separate observations;
either can remain uncertain. A site may read attached bytes before form submission,
and revocation cannot retract prior disclosure. Bounded staged snapshots outlive
open-page use and retire only after browsers close. The
[upload API and config migration](../browser/workspace/README.md#explicit-file-uploads)
own the concrete input shapes and development checks.

Add Chromium hooks and browser-runtime integration only when measured benefits
justify their compatibility and maintenance costs. Authority, visibility or
efficiency already available through existing interfaces is not such a benefit.
The workspace does not depend on native runtime wins. Chromium security and semantics remain browser-owned; workflow
policy cannot move into the GPU runtime or shared authority machinery.

## Versioned observations and action admission

The optional [client trace prototype](../browser/workspace/trace-dag.md) reuses
completed, slot-bound paths above this service. Its structural fingerprints are
lookup hints, not authority. Replay binds fresh observations and retains normal
admission, dispatch and value-retention checks. Final submission is excluded.
Live element metadata adds role, bounded label and input type; the structural
hash and truncation flag are live response fields, not new journal authority.
Existing journals remain readable unchanged.

The opt-in workflow agent may publish a planner-derived trace only after a
caller-provided independent review check succeeds. Structural cache misses and
proven never-dispatched stale actions may return to the planner with a fresh
observation. Unknown effects, pending actions and authorization failures stop
instead. Model output cannot grant permission, renew a revoked grant or authorize
submission. A planner's completion assertion alone does not establish completion.

Opt-in [personal persistence](../browser/workspace/workflow-store.md) isolates
owner/account/template namespaces and adds account context to prefix identity.
Trusted task definitions check account context and slot disclosure before dispatch.
Verified learning publishes under a cross-process lock after reloading the latest
revision, retaining immutable versions and separate run receipts. Persistence
failure never authorizes repeating browser effects. Public imports and automatic
repair publication are excluded; fixture verifiers establish no general ATS oracle.

Observations carry workspace identity, a durable version and the owner generation
under which they were issued. Page changes produce new observations or explicit
versioned changes. Every automated mutation identifies:

```text
workspaceId, actionId, ownerId, ownerGeneration, authorizedIntent,
target, observationVersion, preconditions
```

These names describe required semantics, not a released wire schema. The eventual
protocol must specify target identity, authorization scope, observation expiry,
precondition evaluation and migration before implementation is admitted.

Check ownership, authorization and preconditions again at dispatch, not only at
queue admission. Reject stale or unauthorized requests consistently in every
adapter. A condition that can no longer be established cannot silently pass.
Observation and action are not an atomic browser or external transaction:
intervening page changes, in-flight activity and external effects remain possible.
Report this boundary in results and recovery information.

## Durable ownership and handoff

Serialize handoff with admission and dispatch. Durably advance the owner
generation before admitting the new owner's commands, invalidate queued old-owner
commands, and reject stale generations after restart. Restore durable authority
before accepting any new command; failure to restore it fails closed.

The dispatch/handoff boundary must make ordering observable: a mutation is either
dispatched under the old authority before handoff, or rejected afterward. Handoff
prevents further old-owner dispatch, but cannot undo browser activity or external
requests already underway. Give the new owner outstanding action records and
their current outcome/dispatch status, including uncertainty.

## Action records and uncertain recovery

Persist the authorized action intent before dispatch. Keep dispatch evidence
separate from outcome evidence. Distinguish **completed**, **failed** and
**unknown** outcomes; identify **provably never dispatched** separately. An
exception, timeout, browser loss or missing completion record cannot alone prove
that an external effect did not occur. Persist uncertainty across process restart,
browser reopening and owner handoff.

Missing acknowledgment or completion never authorizes another submission.
Where dispatch or external effects are uncertain, reconcile observable
application state, use supported external idempotency, or request human review.
Record the basis and result of reconciliation. A local action identifier is not
an external idempotency key and cannot prevent duplicate external effects.
Resubmission requires an explicit safe disposition rather than a retry inferred
from absence of success. A genuinely never-dispatched action still requires
current authorization, owner generation and dispatch preconditions.

## Reuse and behavioral acceptance

Implement document research, controlled form submission and artifact export
through the same service and prove consistent behavior across all three. Only
workflow adapters differ. Freeze fixtures,
expected task outcomes, authorization scopes, failure locations and independent
oracles before testing. Every adapter must reject stale commands identically and
preserve unknown outcomes across restart. Required cases include:

- Failure before dispatch, after external submission, and after completion but
  before acknowledgment; establish actual dispatch/effects independently of logs.
- Handoff racing with admission and dispatch, stale queued requests and stale
  observations, restart with stale owner generations, and outstanding requests
  transferred to a new owner.
- Durable intent surviving interruption; uncertain effects staying unknown after
  reopening; reconciliation, supported external idempotency and human review
  preserving safety without duplicate submissions.
- Adapters attempting mutation outside the dispatcher, unauthorized intent and
  targets, violated preconditions and unavailable durable authority.
- Equivalent current commands and failures in each adapter, with workflow logic
  outside the service and no workflow-specific authority bypass.

Use controlled external-effect fixtures with independent receipts to distinguish
submission, completion and acknowledgment. A synthetic log claiming no duplicates
cannot prove that an external action occurred once. Ordinary browser/GPU crash,
device-loss or artwork tests do not meet these requirements.

## Product-value comparison

Compare the implemented workspace against competent stock Chromium/Playwright
automation on the same declared browser/runtime and equivalent useful workflows,
authorization, persistence, recovery opportunities and inputs. Include the
baseline's reasonable state handling and supported idempotency; do not cripple
it to manufacture a gain. Freeze success and safety oracles and material
advantage/regression criteria before execution.

Measure task success, incorrect actions, recovery, observation volume, complete
task latency, service overhead and integration effort. Retain failures, raw
observations, fixture and source identities and uncertainty. Efficient snapshots
or fewer tokens alone cannot establish useful durable work.

The [browser comparison policy](../config/browser-product-comparison-policy.json)
keeps historical A/B/C/D causal lanes and K0 eligibility intact. Their existing
receipts are not workspace-service evidence. Runtime substitution and external
comparators require independently admitted comparisons rather than inherited
claims.

## Selective deeper capabilities

Local computation supports accepted workflows where useful. Doppler owns model
semantics. DoeRuntime integration is explicit and independently qualified;
workspace acceptance does not qualify the runtime or a model. DoeLab tests
improvements offline and DoeProof records the independently evaluated evidence.
Fawn remains the integrated product. Chromium hooks and browser-runtime
integration earn inclusion only through measured, separately attributed workflow
benefits that justify their compatibility and maintenance costs.

## Current admission boundary

The trusted dispatcher, durable ownership generations, versioned-observation
admission and uncertain-action journal are implemented in
[`browser/workspace`](../browser/workspace/README.md). Its protocol section
specifies target identity, authorization scope, observation expiry and scope,
precondition evaluation, migration, adapter credentials, caller authentication
and mutation coverage.

The behavioral gates are frozen in
[`config/fawn-workspace-qualification.json`](../config/fawn-workspace-qualification.json).
It holds cases, authorization scopes, failure locations, expected outcomes, the
fixture site's hash and its independent oracles. The
[qualification harness](../browser/workspace/README.md#qualification) runs every
case through each adapter and workflow. Its semantic validator admits a full
result only for committed source matching the frozen identities, after
re-verifying every journal and reconciling effects with the fixture's own
receipts. The product-value comparison's scenarios, measures and material
criteria are frozen in `workspaceComparison` of
[the comparison policy](../config/browser-product-comparison-policy.json).
[Status](status/fawn-workspace.md) records which results are admitted.

The existing [fixture executor](../bench/fawn_matrix/executors/live_workloads.mjs)
directly dispatches fixture actions; it is benchmark machinery, not this service.
[GPU/browser recovery parity](../browser/chromium/contracts/browser-recovery-parity.contract.md)
has a different scope. Existing browser milestones and historical receipts keep
their original meaning and status.

Workspace completion and release claims remain blocked until the implemented
service and independent behavioral and workflow evidence satisfy this contract.
`fawnWorkspace` in the strategy schema binds each step's assessment to admitted
evidence; passing `bench.tests.test_fawn_workspace_strategy` checks that binding,
not runtime behavior.

## Protocol 4: product clients and page identity

A workspace retains one profile and owner generation, with independently identified
pages. Omitted `pageId` means `main` for existing clients and journals. Observations
have globally monotonic versions but page-local currentness: reading another tab
does not invalidate a page, and a version from another page cannot authorize an
action. All action, completion and reconciliation probes address the recorded page.
Playwright and raw CDP discover popups and retain closed identities. Administrators
can inspect pages, create a blank tab, focus a tab and select a region through the
supported service API. Raw browser handles are never public.

New product workspaces use `scopedReads: true`. Their grants require explicit
`pageIds` and `readRegions` (null means whole page). Reads check page identity,
origin, expiry and revocation before observation and recheck the returned origin.
Region grants disclose only contained text and controls, not the surrounding page.
A popup requires a separate grant. Legacy workspaces preserve their protocol-3 read
behavior; replay does not silently expand or replace their authority.

Form destinations honor the form action unless the submitter explicitly overrides
it. That inherited destination participates in target identity, including regional
actions whose button markup stayed unchanged. Optional journal fields are encoded
before hashing, so the persisted JSON and its hash agree across restart. Existing
valid journal hashes are unchanged; corrupt records remain rejected.

On restart, open page identities are recreated as blank pages. The client shows
the last known URL and requires explicit reopening: restoration never replays a
navigation or external effect. Closed tabs and their evidence remain recorded.

The loopback product client is a separate human and agent client of this protocol.
It keeps source captures, repository evidence, assumptions, findings, decisions and
questions in a separate hash-linked investigation journal. Revisions bind their
source and dependency revisions; corrections mark transitive dependents for review.
Those records do not authorize browser effects or establish that a conclusion is true.

A delegation names pages, origins, regions, allowed actions and a bounded task.
Sharing saved investigation records is a separate explicit choice. Selected local
repository roots are independent read capabilities. The planner receives neither
browser-control handles nor service credentials. Restart takes human control and
makes no model calls; resuming always requires a new delegation. Client development
checks and controlled-planner journeys do not discharge the frozen complete-task
comparison or establish live-model quality, external adoption or Doe GPU ownership.
