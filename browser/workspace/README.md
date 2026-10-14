# Fawn workspace service

The shared observation, action and recovery service above stock Chromium.
[The workspace contract](../../docs/fawn-workspace-contract.md) owns requirements
and acceptance; [status](../../docs/status/fawn-workspace.md) records what is
established. This implementation is **unqualified**: its tests are development
evidence, not the contract's behavioral gates or product-value comparison.

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
- `test/`: unit tests (fake adapter, real process kills) and real-Chromium
  workflow tests with an independent fixture server.

Tunables and protocol coverage come from
[`config/fawn-workspace-service.json`](../../config/fawn-workspace-service.json).

## Protocol v1

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
  it touches: the navigation URL, the current page and, for `submit`, the form
  action. Revoking or expiring a grant before dispatch ends the action
  undispatched.
- **Observations and expiry.** Every `observe` records a new version carrying
  the owner generation, a content hash (URL, DOM and control values), element
  target digests, and an expiry of `observation.maxAgeMs`. An action must cite
  the current, unexpired version from the current generation. At dispatch, the
  service re-reads the page; any change records a `page-change` version and
  rejects the action as `stale-observation`.
- **Target identity.** An element target is a `selector` plus the element
  digest (tag, attributes, text) from the cited observation. At dispatch the
  selector must match exactly one element with the same digest
  (`target-changed` otherwise).
- **Preconditions.** These are declarative `conditionKinds`, evaluated against
  the live page at dispatch. Text and value conditions need exactly one match.
  A condition that throws or times out is unestablished and blocks dispatch.
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
- **Migration.** The journal and config carry `protocolVersion` 1. A later
  protocol must replay or explicitly migrate v1 journals. Unknown record types
  fail closed.

## Commands

```bash
cd browser/workspace
npm ci
npm test               # unit tests: journal, service rules, process-kill restart
npm run test:browser   # adds real headless Chromium through both adapters
```

The browser tests need Playwright's Chromium headless shell
(`npx playwright-core install --only-shell chromium`).

## Known limits

- One page per workspace; popups and additional tabs are only counted.
- Content hashing treats any DOM change as a page change, so continuously
  changing pages make every dispatch stale.
- One process holds a journal; the journal is not compacted.
- No competent stock Chromium/Playwright baseline comparison exists yet.
