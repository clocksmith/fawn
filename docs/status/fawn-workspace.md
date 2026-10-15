# Fawn durable workspace

Fawn provides durable browser workspaces for agents above stock
Chromium/CDP/Playwright. [The contract](../fawn-workspace-contract.md)
owns authority, observation/action binding, recovery and acceptance.
`fawnWorkspace` in [the strategy projection](../../config/doe-product-strategy.json)
owns ordered steps and binds their assessments to admitted evidence.

**Product message:** Fawn preserves trustworthy browser-work outcomes across agent
interruption and gives agents efficient, scoped views of changing applications.

Stale-decision rejection and human handoff are built-in guarantees, not general
advantages: the revalidating control reproduced both.

**Three states stay distinct:**
- **Behaviorally qualified**: established on the frozen case set.
- **Product-advantaged**: established only on the frozen differentiation suites,
  against Playwright agent tooling.
- **Release-qualified**: not established.

The scope is the recorded headless Chromium binary and this Linux host. Scripted
workflows show no material advantage over competent stock automation.

## Service

The shared service is implemented in
[`browser/workspace`](../../browser/workspace/README.md) (protocol 3). It
contains:
- a hash-chained, fsynced journal that restores authority before accepting
  commands and fails closed on corruption or a live lock holder
- owner generations with credential-bound handoff
- grants scoped by intent, action kind and every touched origin, including link
  and form destinations
- versioned, expiring observations with full-text element target digests,
  optional region and region-group digests, and a compact outline view with
  stable handles and deltas against a base version
- a dispatcher that rechecks authority, observation currency (whole page or
  scoped regions), target identity and preconditions at dispatch
- action records that keep never-dispatched, completed, failed and unknown
  separate, with application-state, external-idempotency and human-review
  reconciliation
- narrow Playwright and raw-CDP adapters, both launched over a private pipe
- research, form-submission and export workflow adapters, including form and
  export recovery, that use only the service API

## Behavioral qualification

The [admitted result](../../reports/fawn-workspace/20261015-service-v3-3/README.md) ran the frozen
[case set](../../config/fawn-workspace-qualification.json) through both adapters
and all three workflows on commit `a1f8e4573`, the current service source.
[The semantic validator](../../browser/workspace/qualification/validate.js)
admitted it with no failures. Every requirement of strategy steps 1–4, and the
behavioral requirements of step 5, are established in its
[verdict](../../reports/fawn-workspace/20261015-service-v3-3/qualification/verdict.json).
It requalifies the service after the `url-not-equals` condition and region-group
membership in the outline view. Step 5's suites, the scripted comparison and the
Chrome stable runs keep their own commits and their own
[qualification](../../reports/fawn-workspace/20261015-revalidated-control/qualification/verdict.json)
of the source they ran.

The cases cover:
- process kills before dispatch, while the application holds the effect, and
  after completion before acknowledgment, with the effect established from the
  application's own receipts
- handoff racing admission and dispatch, queued commands ended by handoff or
  grant revocation, and in-flight effects disclosed to the new owner
- stale observations and stale owner generations after restart
- unauthorized intents and origins, violated preconditions, unsupported
  mutation kinds and absent debugging endpoints
- background page changes beside genuine target and scoped-region changes
- reconciliation, external idempotency and human review, and recovery against
  a submissions list longer than any displayed text prefix

Qualification found and fixed these defects:
- A link click could reach an origin outside the grant.
- Conditions read truncated element text, so recovery could judge a submission
  absent and resubmit it.
- A revoked grant was reported as a stale observation.
- An interrupted export rerun reused spent action ids.

## Competent-baseline comparison

The [comparison](../../reports/fawn-workspace/20261015-revalidated-control/comparison/comparison.json), rerun on the same commit, ran
the frozen scenarios and criteria in `workspaceComparison` of
[the comparison policy](../../config/browser-product-comparison-policy.json).
The control was stock Playwright automation with a persistent profile, a progress
file, an origin allowlist, exact locators, the application's idempotency
reference and a submissions check before resubmitting. Both sides used the same
browser binary, fixture inputs, origins, timeout and process shape.

- Success, incorrect actions and recovery do not differ materially in any
  compared scenario, including crashes during submission and export and
  background page changes.
- When the target link is replaced between reading and acting, neither side
  saves the replaced artifact. Fawn stops at dispatch; the control waits for its
  locator timeout. That time to a safe stop is the only material difference
  under the frozen criteria.
- Fawn's per-task overhead is visible on clean runs and stays below the
  material latency rule.
- Owner handoff is excluded because the control has no owner authority; the
  behavioral cases qualify it instead.

Scripted workflows therefore show no material advantage over competent stock
automation.

## Agent-workflow differentiation

`differentiation` in `workspaceComparison` was frozen before implementation
(commit `02239e6dc`), then run in full again on commit `9f522f7d9`
([result](../../reports/fawn-workspace/20261015-revalidated-control/differentiation/differentiation.json)). The workload is a
persistent multipage task on a live workbench site: buy the cheapest valid item
across catalog pages, ship it, and save its invoice. One shared agent policy and
think time drive both sides. The control is the strongest generic agent tooling
on stock Chromium:
- Playwright's page-wide agent snapshot with incremental tracking and aria-ref
  actions
- a browser host that outlives the agent
- durable checkpoints, the application's idempotency reference, application-state
  and current-page checks, and a cooperative pause during human control

The workbench's own orders, cart and mutation log judge every run. Every
requirement was met in every repetition:
- **Changing content.** The chosen item's price rises past the budget while the
  agent thinks. The control's ref still resolves, so it adds the over-budget
  item and must remove it. The service refuses the stale decision because its
  row changed, and the agent re-decides.
- **Incremental observation.** On pages with a large, continuously changing
  activity panel, the region-scoped outline view delivers well under the frozen
  share of the bytes that the control's incremental snapshots carry.
- **Human handoff.** A human enters a shipping address while the agent's fill is
  pending. The control resumes the pending call and overwrites it. The service
  refuses the old owner's call, so the agent observes again and keeps the
  human's address.
- **Interruption recovery.** The agent dies while the application holds its
  order, and the application offers no order list, no idempotency and only a
  transient confirmation. The control must ask for human review. The workspace
  recorded the outcome while the agent was gone, so the agent resumes without
  review or duplicates.

With an order list and an idempotency reference, both sides recover. The frozen
product-advantage rule passed: all four areas met, with no material regression
in the suite or in the scripted comparison rerun on the same commit.

Most service overhead came from the treatment agent's tooling, not the service.
The agent bound each checkout field decision to the whole form, so its own fill
made the next decision stale and cost another think cycle. Binding each field
decision to that field still detects concurrent human edits, and it brought
clean tasks close to the control's latency. The remaining visible cost is
re-deciding after a handoff.

This establishes advantage only for this suite, against this control. It does
not establish broad product superiority, compatibility beyond the fixture or
release qualification. A control harness that revalidates every decision,
discards pending calls on human takeover, or scopes reads with site-specific
locators would narrow these gaps; that is task-specific scripting.

## Second comparison: a stronger control on a different application

`revalidatedDifferentiation` in `workspaceComparison` was frozen before
implementation (commit `a9686d47e`), then run in full on commit `9f522f7d9`
([result](../../reports/fawn-workspace/20261015-revalidated-control/revalidated-differentiation/differentiation.json)).
It uses a helpdesk: claim the oldest open, unclaimed ticket for a product, send
one reply, and save the transcript. Its control is the first suite's Playwright
agent tooling plus two behaviors:
- it revalidates every decision's inputs with a fresh snapshot before mutating
- it cancels a pending call after human control and observes again

Against this control:
- **Changing content: not met.** Another agent claims the chosen ticket during
  the think time. Revalidation sees the claim, and both sides move to the next
  ticket.
- **Human handoff: not met.** A supervisor edits the draft while the send is
  pending. Cancelling and observing again makes both sides send the supervisor's
  text.
- **Incremental observation: met.** Region-scoped outlines carry well under the
  frozen share of the control's snapshot bytes beside a churning feed.
- **Interruption recovery: met.** The agent dies while the application holds the
  reply, and the application offers no reply list, no idempotency and only a
  transient confirmation. The control needs human review in every repetition.
  The workspace's recorded outcome lets the treatment resume without review or
  duplicates.

The suite's frozen rule passed: two areas met in every repetition, with no
material regression in this suite, the first suite or the scripted comparison.
The durable advantage is ownership of outcomes across agent interruption.
Decision binding and handoff safety match what a careful harness can build per
task. A model-driven stage needs its own freeze.

## Model-driven comparison (implemented, not run)

`modelDrivenDifferentiation` in `workspaceComparison` was frozen before
implementation. One model drives both sides with the same:
- model, sampling and token budget
- system prompt and task prompts
- agent-facing tool descriptions (`qualification/model-agent/`, pinned by hash)
- browser binary and task inputs

Only the backends differ. The control keeps the second suite's revalidation and
takeover cancellation. Scenarios cover both development applications and two
held-out applications, a clinic and an expense system. Those are specified now,
implemented only afterwards, and never used for tuning.

The measures include incorrect external effects, human review, recovery,
observation tokens and bytes, latency and total inference cost. Durable outcome
recovery and scoped observation must be met. Held-out workflows and the
stale-decision and handoff scenarios need only non-inferiority; those two are
reported as built-in guarantees, never as advantages.

Suite version 2 sends no sampling parameter, which current models reject, and
requests thinking disabled, which one compatibility request confirms before the
measured run. Spend is approved up to a hard ceiling that the runner enforces
from a ledger. The wiring smoke covers only the procurement and helpdesk clean
scenarios, and the full suite runs once.

The runner, both backends and the held-out fixtures are implemented
([`model-agent/`](../../browser/workspace/qualification/model-agent/)). Each held-out
fixture serves a portal root linking to its first frozen page, because the frozen
task prompts name only the origin. A scripted stand-in model drove every scenario
through both backends offline: every tool, every perturbation and every restart
path. That checks wiring only; it is not evidence about a model.

Every application fixture is bound by hash in `fixtureSites`, and billed runs
refuse a fixture that differs. One known representation difference stays for
this campaign: the treatment's outline names a select control by its option
text, the control's snapshot by its label. Accessible naming changes only in a
later protocol version, with full requalification. The
compatibility request, smoke and full run still need model API credentials,
which this machine does not have.

## Compatibility

The frozen case set passed in full, through both adapters, on Playwright's
Chrome for Testing headless shell and on installed Google Chrome stable, a later
major version with a different headless mode
([verdict](../../reports/fawn-workspace/20261015-revalidated-control/compatibility-chrome/verdict.json)).
It also passed headed on Chrome stable, on a virtual display (Xvfb)
([verdict](../../reports/fawn-workspace/20261015-revalidated-control/compatibility-chrome-headed/verdict.json)).
This is compatibility evidence on this Linux host, not release qualification.
Other engines, operating systems, physical-display sessions and multiple pages
are untested; multi-page support waits until the model-driven comparison is
done.

## Limits and open work

Known limits:
- one page per workspace
- page scope by default: continuously changing pages need regions scope
- page reads wait while Chromium holds a pending navigation
- a single journal holder, no journal compaction, and observation history kept
  per owner generation

Open work:
- the frozen model-driven comparison, waiting for model API credentials
- [proof 3](../thesis.md#three-proofs): stock Chromium with Dawn, the Fawn
  workspace with Dawn, and the Fawn workspace with Doe, under
  [the integrated comparison contract](../../config/fawn-doe-integrated-comparison.json),
  frozen before implementation. Its workloads are the frozen model-driven agent
  suite, unchanged browser Doppler generation, the UMAP SGD workload and a pinned
  upstream Three.js WebGPU application. It needs a forced-Doe Chromium build in
  which the page's WebGPU work is Doe-owned. None exists here: the external
  Chromium lane drive is not attached, the local checkout is empty, and the last
  legacy-shell archive (`browser/chromium/artifacts/20260811T130500Z/`) failed
  clean installation and its smoke checks. Its agent-suite spend needs its own
  approved ceiling.
- broader workloads, compatibility, deployment qualification and selective
  deeper capabilities wait until the proofs resolve
- release admission, which stays blocked

Fawn has scoped workspace advantages, not the final objective: it earns the
larger claim only when the integrated system beats stock Chromium in task
success, latency, memory, recovery and demanding GPU workloads.

Existing [Chromium runtime integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) keep separate evidence. Their receipts do not
establish workspace authority or external-action recovery. Proof 3 builds the
forced-Doe Chromium path for its comparison; adopting Chromium hooks or
browser-runtime integration still waits for measured benefits that justify their
compatibility and maintenance costs. Native compiler/runtime work retains its
own milestone IDs, numerical gates and platform admission.
