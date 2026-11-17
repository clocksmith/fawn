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
source/policy identities (including `trace-dag.js` and `trace-graph-dp.js`),
inputs, results and failures. `workflow.private.json`
contains the reusable HMAC graph and stays private/ignored. A passing
`syntheticReuseEstablished` is scoped to this fixture; the report leaves
`productAdvantageEstablished` false for the broader product claim.

The [retained decision](../../../reports/fawn-workspace/20261109-workflow-reuse/decision.json)
binds the live source, model identity, paired outcomes, independent audit and
initial failed trial. Raw activity and the private graph remain in ignored local
storage; the projection is diagnostic evidence, not portable release qualification.

## Unmodified product revisions

`workflow-reuse-product.js` runs a separate read-only Lucyd component lookup on
Simulatte's committed source before and after `500167e0`. The real change moved
the component library from an always-visible panel into an inert drawer. The
server reproduces the existing `/system-diagram` route rewrite and serves the
committed assets unchanged; it rejects non-GET requests. It does not add test
controls or modify the site to create a performance difference.

```sh
node browser/workspace/qualification/workflow-reuse-product.js --repository ../simulatte --out /tmp/lucyd-workflow-preflight --controlled
node browser/workspace/qualification/workflow-reuse-product.js --repository ../simulatte --out /tmp/lucyd-workflow-sonnet
```

The paid command uses the same planner port, model, input slots and authorized
control projection for Fawn and fresh stock Playwright. It also retains a semantic
stock-script baseline that uses the existing search label and drawer control.
That script's authoring cost is unmeasured, so its elapsed time is a reference
floor rather than a claim about total human effort. Both browser paths use stock
Chrome; this exercise establishes no Doe or Chromium-fork advantage.

The independent verifier reads the committed component catalog directly and
compares expected result IDs with the actual rendered list. It also checks the
query, open drawer and unchanged diagram. Only those verified results can teach
repairs. Every Fawn task opens a fresh service/browser profile and reconstructs
its private workflow store; later related queries must retain the repaired path
without planner calls. Totals charge initial learning, changed-page repair,
later tasks, browser/service setup, persistence and cleanup.

Workflow scope comes from the host's disclosure rules, never model suggestions.
Unrelated controls no longer define recipe identity. Hidden, inert and disabled
controls remain visible in the general service observation with actionability
metadata, but are omitted from workflow planning and replay. Original observation
versions, text, truncation, account checks and per-action authorization remain in
force. Changing the allowed controls changes the recipe scope. Existing unscoped
recipes safely miss and relearn; an arbitrary verifier failure or uncertain
effect still stops and teaches nothing.

Retain all failed runs and source hashes. If another workload overlaps a run,
record its start/end and qualify the timing separately: functional correctness,
planner-call counts and observed provider charges do not establish an
uncontaminated speed improvement. A faster scripted baseline must remain visible
in the result; this bounded local-product case cannot establish general browser
superiority or live-site compatibility.

The 2026-11-16 source-bound run at `22ce1ad219` completed all 18 checks. Including
learning and the changed-drawer repair, Fawn used five planner calls billed at
$0.0978277; fresh stock planning used seventeen billed at $0.2740556. All four
later Fawn queries reused the persisted repair without planner calls. The stock
semantic script passed all six tasks with zero planner calls and $0 provider
charges. Its authoring cost was not measured: this result supports a benefit over
fresh replanning, not a win over a prepared script.

Elapsed-time comparisons from that run are rejected for two reasons: a concurrent
handoff trial overlapped it, and stock launch deleted a temporary Chrome profile
while Fawn retained its persistent profile. A zero-model lifecycle diagnostic
isolated 10.7–11.0 seconds in stock `browser.close()`, versus 10–17 ms closing its
page and 46–59 ms closing its context. Playwright's ordinary launch registers the
generated user-data directory for deletion and waits for cleanup at browser close.
This explains a systematic lifecycle asymmetry; it does not establish the entire
cause of the original 179-second row.

The reproducer now retains a fresh profile in both arms and records separate
page, context, browser, service and verification phases. The matched zero-model
follow-up closed stock contexts in 97–116 ms and the already-disconnected browser
in about 1 ms, with no downloads or remaining pages. No paid comparison was
rerun. Use `--lifecycle-only` for that focused diagnostic without model usage.
Original receipts, failed readiness probes, journals, both diagnostics and a hash
manifest are retained locally under `.tmp/track-c-workflow-20261116`; that private
archive is intentionally outside Git. Keep its `run-context.json` timing verdict
with the raw summary rather than promoting the raw elapsed totals into a claim.
