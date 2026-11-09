# Sonnet workflow reuse proof

The owner-requested proof is a familiar application completed reliably with fewer
planner calls after one successful learning run. It compares reuse against fresh
Sonnet planning above the same stock Chrome/service stack. It does not compare
browser engines or claim support for live Greenhouse or Ashby sites.

The frozen [policy](../../../config/fawn-workflow-reuse-comparison.json) supplies
the task, Sonnet model selector, synthetic applicants, approved intermediate
clicks, graph bounds, request/spend limits, cases and acceptance criteria. The CLI
provider must report actual Sonnet model identities. It has no native tools,
shell, browser access, MCP or repository context. Both arms receive the same
slot-aware tool contract and fresh observation before each planner request.

```sh
node browser/workspace/qualification/workflow-reuse-compare.js --controlled --out bench/out/workflow-preflight
node browser/workspace/qualification/workflow-reuse-compare.js --out bench/out/workflow-sonnet
```

The controlled command checks the harness without model calls or a speed claim.
The live command explicitly incurs Sonnet usage through the authenticated Claude
CLI. It freezes source hashes and policy before learning, preserves failed runs,
records provider usage and accounts for worst-case cost of failed calls. Do not
overwrite a prior output directory when rerunning an experiment.

## Work and boundaries

Sonnet first fills contact details, advances to required application questions,
and reaches the review screen. The client records live verified actions with
named input slots; it does not mine the old service journal or fit a hand-authored
sequence. An independent oracle reads the rendered review field/value pairs and
compares them with the synthetic applicant. Only then can learning publish a
trace. A premature model `finish` fails rather than becoming a reusable workflow.

The graph is frozen throughout evaluation. Each arm starts a new workspace,
browser profile and explicit grant, with new requisition-specific IDs. Pair inputs
are identical; learning uses different applicant details. Final submission is
absent from the grant, the recording API rejects native submit targets, and the
fixture independently counts any submission request. No account identity is
inferred from service ownership.

Profile inputs may include fields an application never requests. The task requires
review of requested fields, not display of every profile slot. The initial live
trial exposed ambiguous wording here: Sonnet reached review but asked for help
about the unused phone slot. That failed trial is retained separately; the oracle
and required form fields were not relaxed when clarifying the task.

Three familiar pairs alternate arm order. One pair per fault then exercises:

- Changed fields: a new required phone question in the second stage. Reuse must
  stop matching and explicitly ask Sonnet to plan from the new page.
- Stale page: the harness changes page text after observation and before the
  first mutation. The service must record a never-dispatched stale refusal;
  both arms may ask Sonnet for a new decision from a fresh observation.
- Revoked permission: the grant is removed before the first mutation dispatch.
  The run must request attention, with no automatic renewal or further planning.
- Uncertain outcome: the first intermediate click actually runs, then the test
  adapter loses its acknowledgment. The service must retain one unknown action,
  with no repeated effect or further planner call.

The injected page mutation and acknowledgment loss are declared harness faults,
not alternate routes for performing task actions. Every task mutation uses the
real service admission, dispatch and 300 ms value-settling checks.

## Measurements and decision

Retain correct review completion, model calls, cache steps, preparation time,
task time, complete-task time including preparation, human intervention requests,
actual human interventions, unknown actions and post-fault actions/calls. No human
corrections are supplied during the run. Safe stops are expected behavior for
revoked permission and uncertainty; they are never counted as completed forms.

All cases must pass their independent expected behavior with zero submissions.
Each familiar pair must complete correctly, use zero planner calls during reuse,
finish faster including preparation, and request no additional human help. The
report separates the up-front learning cost from reuse savings. Provider caching
is uncontrolled, and three familiar pairs cannot establish population p95 or
real-site reliability. Changed/stale latency is reported without requiring a win.

`frozen.json`, per-run `result.json`, `planner-calls.json` and `report.json` retain
source/policy identities, inputs, results and failures. `workflow.private.json`
contains the reusable HMAC graph and stays private/ignored. A passing
`syntheticReuseEstablished` is scoped to this fixture; the report leaves
`productAdvantageEstablished` false for the broader product claim.
