# Fawn durable workspace

Fawn provides durable browser workspaces for agents above stock
Chromium/CDP/Playwright. [The contract](../fawn-workspace-contract.md)
owns authority, observation/action binding, recovery and acceptance.
`fawnWorkspace` in [the strategy projection](../../config/doe-product-strategy.json)
owns ordered steps; every assessment remains unestablished.

The shared service is **implemented and unqualified** in
[`browser/workspace`](../../browser/workspace/README.md) (protocol v1). It contains:
- a hash-chained, fsynced journal that restores authority before accepting
  commands and fails closed on corruption or a live lock holder
- owner generations with credential-bound handoff
- grants scoped by intent, action kind and origin
- versioned, expiring observations with element target digests
- a dispatcher that rechecks authority, observation currency, target identity
  and preconditions at dispatch
- action records that keep never-dispatched, completed, failed and unknown
  separate, with application-state, external-idempotency and human-review
  reconciliation
- narrow Playwright and raw-CDP adapters, both launched over a private pipe
- document-research, form-submission and artifact-export workflow adapters
  that use only the service API

Strategy steps 1–4 are `implemented-unqualified`; reuse/product value and
selective deeper capabilities are not implemented.

Development evidence comes from `npm test` and `npm run test:browser` in
`browser/workspace`:
- Unit tests use a fake adapter and kill real processes at three points:
  after intent, after the external effect, and after the outcome but before
  acknowledgment.
- The browser tests run against headless Chromium through both adapters, with
  a fixture server that keeps its own submission receipts. They cover:
  - all three workflows
  - identical `stale-observation` and `stale-owner-generation` rejection
    across them
  - a SIGKILL while the server holds a submission
  - a restart in which the outcome stays `unknown`
  - a refused resubmission
  - reconciliation from the application's submissions page, with no
    duplicate receipt

These tests are not the contract's behavioral gates.

Missing for qualification:
- a frozen, admitted fixture set with expected outcomes, authorization scopes,
  failure locations and independent oracles
- handoff races at scale and stale-queue cases beyond the unit tests
- the competent stock Chromium/Playwright baseline comparison and its measures
- a semantic evidence validator that admits results into the strategy

Known limits: one page per workspace, whole-DOM content hashing (continuously
changing pages make every dispatch stale), a single journal holder, and no
journal compaction. Workspace completion and release claims remain blocked.

Existing [Chromium runtime integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) keep separate evidence. Their receipts do not
establish workspace authority or external-action recovery. Chromium hooks and
browser-runtime integration wait for measured benefits that justify their
compatibility and maintenance costs. Native compiler/runtime
work retains its own milestone IDs, numerical gates and platform admission.
