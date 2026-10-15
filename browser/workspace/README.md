# Fawn workspace service

The shared observation, action and recovery service above stock Chromium.
[The workspace contract](../../docs/fawn-workspace-contract.md) owns requirements
and acceptance; [status](../../docs/status/fawn-workspace.md) records what is
established. Its behavior is qualified on the frozen case set for a recorded
commit, browser binary and host; product value over competent automation is not
established. Development tests are not the behavioral gates.

## Layout

- `src/service.js`: `WorkspaceService`, the only holder of the workspace browser.
  It owns admission, the trusted dispatcher, handoff, observation recording and
  reconciliation.
- `src/journal.js`, `src/state.js`: the durable hash-chained journal and its
  pure state projection.
- `src/admission.js`, `src/conditions.js`, `src/authority.js`: checks shared by
  admission and dispatch, declarative page conditions, and credentials.
- `src/adapters/`: narrow browser adapters (Playwright, and raw CDP over
  `--remote-debugging-pipe`) plus the shared in-page reader.
- `src/workflows/`: document research, controlled form submission and artifact
  export. They contain the task logic and reach the browser only through the
  service API.
- `qualification/`: the behavioral qualification harness and the
  competent-baseline comparison (see [Qualification](#qualification)).
- `test/`: unit tests (fake adapter, real process kills) and a real-Chromium
  qualification subset that also checks the validator rejects tampered evidence.

Tunables and protocol coverage come from
[`config/fawn-workspace-service.json`](../../config/fawn-workspace-service.json).

## Protocol v3

The contract's semantic names map to these concrete rules.

- **Workspace identity.** `workspaceId` (`[a-z0-9-]+`) names one journal entry,
  one persistent Chromium profile under the state directory, and one page.
- **Caller authentication.** Creating a workspace returns an admin credential;
  each `assignOwner` returns an owner credential bound to one owner generation.
  Only SHA-256 hashes are journaled. Owner commands present `ownerId`,
  `ownerGeneration` and the credential; a credential from an earlier generation
  is `stale-owner-generation`, and any other credential is `unauthenticated`.
- **Adapter credentials and mutation coverage.** The service constructs its
  adapter from a factory and never exposes it. Chromium is launched over a
  private pipe, so no debugging port exists for another client. Automated
  mutations are limited to `actionKinds` in the config: `navigate`, `click`,
  `fill`, `select`, `submit` and `download`. A control that submits a form must
  use `submit`. Anything else is `unsupported-action-kind`. Human browser
  activity remains possible and shows up as page changes.
- **Authorization scope.** An admin grant names one owner, a set of intents,
  action kinds and exact target origins, with an optional expiry. An action needs
  one live grant that covers its `authorizedIntent`, its `kind` and every origin
  it touches: the navigation URL, the current page, the destination of a
  followed link (`click`, `download`) and, for `submit`, the form action. The
  intent check runs before the observation check, so a revoked or expired grant
  is reported as `unauthorized-intent` whatever the page did. Revoking or
  expiring a grant before dispatch ends the action undispatched.
- **Observations and expiry.** Every `observe` records a new version carrying
  the owner generation, a content hash (URL, DOM and control values), element
  target digests, link and form destinations, and an expiry of
  `observation.maxAgeMs`. `observe` may name up to `observation.maxRegions`
  region selectors; the version records a digest of each region's markup and
  control values, or none when the selector does not match exactly one element.
- **Observation scope.** An action's `observationScope` (one of
  `observationScopes`) binds its staleness:
  - `page` (the default): the cited version must be the current one. At
    dispatch the service re-reads the page; any change records a `page-change`
    version and rejects the action as `stale-observation`.
  - `regions`: the cited version may be older, but it must come from the current
    generation, be unexpired and have recorded every scoped region. Later
    versions and the live page at dispatch must keep the cited URL and every
    scoped region digest; otherwise the action is `stale-observation`. Changes
    outside the scoped regions are still recorded as `page-change` versions.
  An empty regions list means the action depends on no page content beyond the
  cited URL; navigation uses it on continuously changing pages. Target identity
  is checked in both scopes.
- **Views for agents.** `observe` also accepts `regionGroups`: every element
  matching a group selector is recorded as its own region under its stable
  selector, for example each product row. `view: 'outline'` returns only:
  - the version, URL and non-atomic boundary
  - each region as a compact outline of its text, with interactive elements
    written as `[handle role "name" = "value"]`

  With `sinceVersion`, a region whose digest matches that version is returned
  as `unchanged`. Digests, element tables and handle tables stay in the journal.
- **Handles.** A handle names an element by a hash of its stable selector, so it
  survives unrelated page changes. An action target `{handle}` resolves through
  the cited observation's handle table to that observation's selector and
  digest, and dispatch checks it like an explicit target.
- **Target identity.** An element target is a `selector` plus the element
  digest (tag, attributes, displayed text and a fingerprint of the full text)
  from the cited observation. At dispatch the selector must match exactly one
  element with the same digest (`target-changed` otherwise).
- **Preconditions.** These are declarative `conditionKinds`, evaluated against
  the live page at dispatch. Text and value conditions need exactly one match
  and read the element's full text; `element-text-excludes` is positive
  evidence of absence only for that one element. A condition that throws or
  times out is unestablished and blocks dispatch.
- **Action records.** Intent is journaled (`action-admitted`) before queueing,
  and `action-dispatch-started` is journaled before the browser effect. Outcomes
  are `completed` or `failed` only with positive evidence: completion or failure
  conditions, a verified value, or a saved artifact hash. Exceptions, timeouts
  and restarts after dispatch began are `unknown`. Queued actions that end at
  dispatch, handoff or restart are `never-dispatched`.
- **Handoff.** `assignOwner` serializes with admission and dispatch. It
  advances the generation durably and ends queued commands. It returns the
  outstanding records, including in-flight and unknown ones, and the
  non-atomic boundary statement.
- **External effects.** An external effect needs an `effectKey` and completion
  conditions. A new attempt for that effect is refused while an earlier attempt
  is queued, in flight or unknown (`effect-unresolved`), or completed
  (`effect-already-completed`). A permitted retry must name the prior attempt
  in `resubmissionOf`.
- **Reconciliation.** Unknown outcomes stay recorded. Three methods add
  reconciliation records with their basis:
  - `application-state`: conditions on the live page.
  - `external-idempotency`: the original action's external key, never its
    `actionId`. The retry must reuse that key.
  - `human-review`: decided by the workspace admin.
- **Restart.** `WorkspaceService.open` verifies the whole journal chain, ends
  queued actions and marks dispatched actions without outcome `unknown`, and only
  then accepts commands. A corrupt record or a live lock holder fails closed. A
  final record without its terminator was never acted on and is discarded with
  a recorded hash.
- **Migration.** Protocol 2 added observation regions, `observationScope`, link
  destinations and full-text fingerprints. Protocol 3 adds region groups, the
  outline view with deltas, handle targets and empty regions scopes. Earlier
  journals replay unchanged. Their observations carry no handles, and an
  observation without recorded link destinations cannot authorize a `click` or
  `download`. Unknown record types fail closed.

## Qualification

[`config/fawn-workspace-qualification.json`](../../config/fawn-workspace-qualification.json)
freezes the behavioral case set: cases, authorization scopes, failure locations,
expected outcomes, the fixture site's hash and the oracles. Each case names a
procedure in `qualification/procedures.js` and runs through every adapter and
every listed workflow.

- `fixture-site.js` serves the corpus, the contact form, export pages and a live
  export page that changes in the background, on a main and a foreign origin.
  Its own request log and receipts are the independent oracle.
- `service-host.js` runs the service in a separate process. The runner kills it
  at declared points, reopens its state and issues concurrent commands so
  handoff can race admission and dispatch.
- `identity.js` binds results to source trees, configs, the commit and the
  browser executable actually running under the host, read from `/proc`.
- `validate.js` admits a full result only if it names committed source and
  matches the frozen case set and fixture. For every run it then:
  - re-verifies and replays the journal and checks ordering invariants
  - compares what callers saw, the journal and the fixture's logs with the
    expectations
  - judges external effects by the application's receipts
  - requires identical behavior across adapters and no debugging endpoint

  It reports coverage of every strategy requirement.
- `compare.js` and `baseline/` compare the same workflows against competent
  stock Playwright automation, under the frozen scenarios and criteria in
  `workspaceComparison` of
  [the comparison policy](../../config/browser-product-comparison-policy.json).
  The baseline keeps a persistent profile, a durable progress file, an origin
  allowlist, exact locators, the application's idempotency reference and a
  submissions check before resubmitting.
- `differentiate.js`, `workbench-site.js` and `agents/` run the frozen
  differentiation suite (`workspaceComparison.differentiation`).
  - Workload: multipage agent workflows on a live workbench site, judged by that
    site's own orders, cart and mutation log.
  - Agent: one shared policy (`agents/procurement-policy.js`) decides on both
    sides with the same think time.
  - Treatment: `agents/fawn-agent.js`, a client of this service using outline
    views, region-bound decisions and ownership handoff.
  - Control: `agents/playwright-agent.js` with `agents/playwright-tool-host.js`,
    using Playwright's page-wide incremental agent snapshots and aria-ref actions
    on a browser host that outlives the agent.

## Commands

```bash
cd browser/workspace
npm ci
npm test               # unit tests: journal, service rules, process-kill restart
npm run test:browser   # adds a qualification subset through both adapters
npm run qualify        # full case set; writes bench/out/fawn-workspace-qualification/<stamp>
npm run compare        # baseline comparison; writes bench/out/fawn-workspace-comparison/<stamp>
npm run differentiate  # differentiation suite; writes bench/out/fawn-workspace-differentiation/<stamp>
node qualification/validate.js <resultDir> --require-current-source
```

The browser tests need Playwright's Chromium headless shell
(`npx playwright-core install --only-shell chromium`). Qualification reads
`/proc`, so it runs on Linux.

## Known limits

- One page per workspace; popups and additional tabs are only counted.
- Page scope treats any DOM change as a page change, so continuously changing
  pages need `regions` scope with the regions an action depends on.
- Chromium answers no page evaluation while a navigation is pending, so
  observations and probes wait for it to commit, bounded by
  `dispatch.actionTimeoutMs`.
- One process holds a journal; the journal is not compacted, and observation
  history is kept for the current owner generation.
