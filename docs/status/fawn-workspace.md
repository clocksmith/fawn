# Fawn durable workspace

Fawn provides durable browser workspaces for agents above stock
Chromium/CDP/Playwright. [The contract](../fawn-workspace-contract.md)
owns authority, observation/action binding, recovery and acceptance.
`fawnWorkspace` in [the strategy projection](../../config/doe-product-strategy.json)
owns ordered steps and binds their assessments to admitted evidence.

**Three states stay distinct:**
- **Behaviorally qualified**: established on the frozen case set.
- **Product-advantaged**: established only on the frozen differentiation suite,
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

The [admitted result](../../reports/fawn-workspace/20261015-agent-differentiation/README.md) ran the frozen
[case set](../../config/fawn-workspace-qualification.json) through both adapters
and all three workflows on commit `59b95f9ce`.
[The semantic validator](../../browser/workspace/qualification/validate.js)
admitted it with no failures. Every requirement of strategy steps 1–4, and the
behavioral requirements of step 5, are established in its
[verdict](../../reports/fawn-workspace/20261015-agent-differentiation/qualification/verdict.json).

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

The [comparison](../../reports/fawn-workspace/20261015-agent-differentiation/comparison/comparison.json), rerun on the same commit, ran
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
(commit `02239e6dc`), then run in full on commit `59b95f9ce`
([result](../../reports/fawn-workspace/20261015-agent-differentiation/differentiation/differentiation.json)). The workload is a
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
in the suite or in the scripted comparison rerun on the same commit. The service
costs latency on every task. The clean and churn runs sit just inside the frozen
latency rule, so later service overhead can turn them into a regression.

This establishes advantage only for this suite, against this control. It does
not establish broad product superiority, compatibility beyond the fixture or
release qualification. A control harness that revalidates every decision,
discards pending calls on human takeover, or scopes reads with site-specific
locators would narrow these gaps; that is task-specific scripting.

## Second comparison (frozen before implementation)

`revalidatedDifferentiation` in `workspaceComparison` tests whether these
advantages survive a stronger control on a different application. The
application is a helpdesk. The control is Playwright agent tooling that:
- revalidates decision inputs with a fresh snapshot before every mutation
- cancels a pending call and observes again after human control

It keeps the four target areas, with requirements set before implementation. A
model-driven stage needs its own later freeze.

## Limits and open work

Known limits:
- one page per workspace
- page scope by default: continuously changing pages need regions scope
- page reads wait while Chromium holds a pending navigation
- a single journal holder, no journal compaction, and observation history kept
  per owner generation

Open work:
- broader workloads, compatibility and deployment qualification
- selective deeper capabilities
- release admission, which stays blocked

Existing [Chromium runtime integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) keep separate evidence. Their receipts do not
establish workspace authority or external-action recovery. Chromium hooks and
browser-runtime integration wait for measured benefits that justify their
compatibility and maintenance costs. Native compiler/runtime work retains its
own milestone IDs, numerical gates and platform admission.
