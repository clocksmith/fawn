# Fawn durable workspace

Fawn provides durable browser workspaces for agents above stock
Chromium/CDP/Playwright. [The contract](../fawn-workspace-contract.md)
owns authority, observation/action binding, recovery and acceptance.
`fawnWorkspace` in [the strategy projection](../../config/doe-product-strategy.json)
owns ordered steps and binds their assessments to admitted evidence.

**Behavior is qualified on the frozen case set; product value is not
established.** The scope is the recorded headless Chromium binary and this Linux
host, not release qualification.

## Service

The shared service is implemented in
[`browser/workspace`](../../browser/workspace/README.md) (protocol 2). It
contains:
- a hash-chained, fsynced journal that restores authority before accepting
  commands and fails closed on corruption or a live lock holder
- owner generations with credential-bound handoff
- grants scoped by intent, action kind and every touched origin, including link
  and form destinations
- versioned, expiring observations with full-text element target digests, and
  optional region digests
- a dispatcher that rechecks authority, observation currency (whole page or
  scoped regions), target identity and preconditions at dispatch
- action records that keep never-dispatched, completed, failed and unknown
  separate, with application-state, external-idempotency and human-review
  reconciliation
- narrow Playwright and raw-CDP adapters, both launched over a private pipe
- research, form-submission and export workflow adapters, including form and
  export recovery, that use only the service API

## Behavioral qualification

The [admitted result](../../reports/fawn-workspace/20261014-behavioral-qualification/README.md)
ran the frozen [case set](../../config/fawn-workspace-qualification.json)
through both adapters and all three workflows on commit `973a8fb29`.
[The semantic validator](../../browser/workspace/qualification/validate.js)
admitted it with no failures. Every requirement of strategy steps 1–4, and the
behavioral requirements of step 5, are established in its
[verdict](../../reports/fawn-workspace/20261014-behavioral-qualification/qualification/verdict.json).

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

The [comparison](../../reports/fawn-workspace/20261014-behavioral-qualification/comparison/comparison.json)
ran the frozen scenarios and criteria in `workspaceComparison` of
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

The comparison therefore does not establish product value over competent stock
automation for these workflows. Step 5 stays unestablished.

## Limits and open work

Known limits:
- one page per workspace
- page scope by default: continuously changing pages need regions scope
- page reads wait while Chromium holds a pending navigation
- a single journal holder, no journal compaction, and observation history kept
  per owner generation

Open work:
- product value over competent automation
- selective deeper capabilities
- release admission, which stays blocked

Existing [Chromium runtime integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) keep separate evidence. Their receipts do not
establish workspace authority or external-action recovery. Chromium hooks and
browser-runtime integration wait for measured benefits that justify their
compatibility and maintenance costs. Native compiler/runtime work retains its
own milestone IDs, numerical gates and platform admission.
