# Fawn durable workspace contract

Status: **requirements only; shared service not implemented or qualified**.
This contract owns prospective shared-service behavior and acceptance.
[Goals](../GOALS.md) own product intent, [the thesis](thesis.md) owns rationale,
and `fawnWorkspace` in [the strategy projection](../config/doe-product-strategy.json)
owns ordered steps and their current assessments. Do not add a competing roadmap.

## Service and workflow boundaries

Fawn is the integrated environment where humans and agents perform durable
browser work. Begin with a trusted workspace service above stock Chromium, with
narrow CDP/Playwright adapters. Persist workspace identity, authorization,
ownership generation, observation versions and action records. Workflow adapters
implement task-specific logic using the same machinery.

The service is an authority boundary, not another unconstrained automation
client. All Fawn-controlled automated browser mutations pass through its trusted
dispatcher. Human browser activity and unrelated external actors remain possible
and must be accounted for by observation and preconditions. Direct CDP access
cannot be an undocumented route around Fawn's dispatch checks. Define adapter
credentials, caller authentication and mutation coverage before runtime admission.

Browser hooks require a demonstrated need for authority, visibility or efficiency
unavailable through existing interfaces. The workspace does not depend on native
runtime wins. Chromium security and semantics remain browser-owned; workflow
policy cannot move into the GPU runtime or shared authority machinery.

## Versioned observations and action admission

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
through the same service. Only workflow adapters differ. Freeze fixtures,
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
workspace acceptance does not qualify the runtime or a model. DoeLab develops
offline corrections and DoeProof records independently evaluated adoption
evidence. Fawn remains the integrated product. Chromium hooks and runtime
replacement earn inclusion through demonstrated need and separately attributed
workflow value.

## Current admission boundary

No trusted dispatcher, durable ownership service, versioned-observation admission
or uncertain-action journal is implemented for this contract. The existing
[fixture executor](../bench/fawn_matrix/executors/live_workloads.mjs) directly
dispatches fixture actions; it is benchmark machinery, not this service.
[GPU/browser recovery parity](../browser/chromium/contracts/browser-recovery-parity.contract.md)
has a different scope. Existing browser milestones and historical receipts retain
their original meaning and status.

Workspace completion and release claims remain blocked until the implemented
service and independent behavioral/workflow evidence satisfy this contract.
The current strategy schema deliberately rejects a completed/qualified workspace
assessment: admitting future implemented evidence requires a versioned contract
extension and an actual semantic evidence validator. Passing
`bench.tests.test_fawn_workspace_strategy` checks prospective intent only.
