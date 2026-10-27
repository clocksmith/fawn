# Fawn workspace service

The shared observation, action and recovery service above stock Chromium.
This is Fawn's workspace owner inside
[clocksmith/fawn](https://github.com/clocksmith/fawn). Doe, developed in the same
repository, owns the independently usable native WebGPU runtime and WGSL compiler.
The service is JavaScript: an end-to-end C GPU route means selecting Doe's C
compiler/runtime in the browser, not rewriting workspace or Chromium code in C.
[The workspace contract](../../docs/fawn-workspace-contract.md) owns requirements
and acceptance; [status](../../docs/status/fawn-workspace.md) records what is
established. Its behavior is qualified on the frozen case set for a recorded
commit, browser binary and host. It shows complete-task value over Playwright
agent tooling only on the frozen differentiation suite, and none over competent
scripted automation. Development tests are not the behavioral gates.

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
  private pipe, so no debugging port exists for another client. `browser.channel`
  names the build both adapters launch: Playwright's headless shell, Chromium,
  installed Google Chrome, or an explicit hash-bound Fawn build. Automated
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
  - `regionGroups`: each requested group selector with the stable selectors of
    the regions it produced, so a scope can cite a group's members

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
- `identity.js` binds results to source trees, configs, the commit, running
  browser images and mapped browser code. `process-inspection.js` observes
  processes and listeners through Linux `/proc`, macOS libproc/lsof, or Windows
  CIM/NetTCPIP; adapter assertions do not establish identity.
- `validate.js` admits a full result only if it names committed source and
  matches the frozen case set and fixture. For every run it then:
  - re-verifies and replays the journal and checks ordering invariants
  - compares what callers saw, the journal and the fixture's logs with the
    expectations
  - judges external effects by the application's receipts
  - requires identical behavior across adapters and no debugging endpoint

  It reports coverage of every strategy requirement. Retained evidence paths
  use the producer's OS grammar and must stay inside their evidence directory,
  including after symlink resolution. An optional `case-set.json` snapshot must
  hash to the producer's recorded bytes and contain exactly the current frozen
  rules. This permits cross-platform line endings without changing receipts or
  accepting different expectations. `--require-current-source` additionally
  checks the live service/config/harness identities; offline revalidation does
  not transfer a result to newer source.
- `compare.js` and `baseline/` compare the same workflows against competent
  stock Playwright automation, under the frozen scenarios and criteria in
  `workspaceComparison` of
  [the comparison policy](../../config/browser-product-comparison-policy.json).
  The baseline keeps a persistent profile, a durable progress file, an origin
  allowlist, exact locators, the application's idempotency reference and a
  submissions check before resubmitting.
- `differentiate.js`, `differentiation-suites.js` and `agents/` run the frozen
  differentiation suites in `workspaceComparison`:
  - `differentiation`: a procurement workbench (`workbench-site.js`)
  - `revalidatedDifferentiation`: a helpdesk (`helpdesk-site.js`) against a
    stronger control that revalidates decisions before mutating and cancels
    pending calls on human takeover

  Each agent has a core (`fawn-agent.js`, `playwright-agent.js`) and a
  per-application profile (`procurement-tools.js`, `helpdesk-tools.js`).
  - Workload: multipage agent workflows on a live site, judged by that site's
    own records.
  - Agent: one shared policy per application (`agents/procurement-policy.js`,
    `agents/helpdesk-policy.js`) decides on both sides with the same think time.
  - Treatment: `agents/fawn-agent.js`, a client of this service using outline
    views, region-bound decisions and ownership handoff.
  - Control: `agents/playwright-agent.js` with `agents/playwright-tool-host.js`,
    using Playwright's page-wide incremental agent snapshots and aria-ref actions
    on a browser host that outlives the agent.
- `model-agent/` runs `modelDrivenDifferentiation`: one model drives both sides
  through the frozen prompts and tools (`differentiate.js --suite
  modelDrivenDifferentiation`):
  - `agent-loop.js`: the checkpointed tool loop, restart notice, turn limit and
    input budget
  - `backends.js`: the treatment over this service, and the control over the
    tool host with revalidation and takeover cancellation, refusing with the
    same words
  - `apps.js`: per-application perturbation points and judges; the held-out
    fixtures are in `held-out/`
  - `anthropic-client.js` and `spend-ledger.js`: the frozen request fields,
    bounded retries and a durable ledger that refuses any request whose worst
    case could cross the approved ceiling
  - `model-driven.js`: modes `compatibility`, `smoke`, `full` (once) and
    `offline`. Billed modes need committed source and run in that order.
    `offline` drives any scenario with `scripted-model.js`, a stand-in that
    checks wiring and proves nothing about a model.

## Commands

```bash
cd browser/workspace
npm ci
npm test               # unit tests: journal, service rules, process-kill restart
npm run test:browser   # adds a qualification subset through both adapters
npm run test:browser -- --browser-channel chrome # use installed Chrome for both arms/adapters
npm run qualify        # full case set; writes bench/out/fawn-workspace-qualification/<stamp>
npm run compare        # baseline comparison; writes bench/out/fawn-workspace-comparison/<stamp>
npm run differentiate  # first suite; --suite revalidatedDifferentiation runs the second
npm run differentiate -- --suite modelDrivenDifferentiation --mode offline --scenarios clinic-clean   # wiring, no API
npm run differentiate -- --suite modelDrivenDifferentiation --mode compatibility   # then smoke, then full
npm run qualify -- --browser-channel chrome   # the case set on installed Google Chrome
xvfb-run -a npm run qualify -- --browser-channel chrome --headless false   # headed, on a virtual display
node qualification/validate.js <resultDir> --require-current-source
```

The browser tests use Playwright's Chromium headless shell by default
(`npx playwright-core install --only-shell chromium`), or installed Chrome with
`--browser-channel chrome`. Channel selection applies equally to both adapters
and both comparison arms; it does not weaken the browser sandbox.
Qualification observes owned processes, actual executable images and TCP listeners
through `/proc` on Linux, `ps`/`lsof` plus libproc through Python3 on macOS,
and CIM/NetTCPIP on Windows. macOS uses kernel executable paths rather than
mutable/truncated process titles.
macOS browser roots, helper images and mapped app frameworks have separate hashes.

### Explicit C browser integration

Service configuration schema 4 adds `browser.channel: "fawn"` and a required
`fawnBuild` containing an absolute browser image path and SHA-256, an explicit
array of dependency file identities, and `runtime.mode: "doe-c"` with the C
library identity. Include the compiler and complete private browser/runtime
closure, including macOS frameworks or Windows `chrome.dll`. Use the actual
browser image, rather than a shell script, `.cmd` or product launcher: qualification
compares it with the executable observed by the OS. Both adapters recheck all
declared files before launch, force Doe with the selected library, keep the
sandbox enabled and reject missing or changed artifacts. Neither adapter retries
with another browser or provider. Explicit `runtime.mode: "dawn"` supplies the
rollback route and accepts no Doe library.

```bash
node qualification/create-browser-profile.js --executable /absolute/browser/image \
  --runtime doe-c --library /absolute/libwebgpu_doe_native.so \
  --dependency /absolute/libdoe_compiler.so --out /absolute/new-c-profile.json
npm run qualify -- --browser-config /absolute/new-c-profile.json
npm run compare -- --browser-config /absolute/new-c-profile.json
npm run differentiate -- --browser-config /absolute/new-c-profile.json
```

Repeat `--dependency FILE` for every additional packaged dependency. The generator
hashes real files and refuses to overwrite its output. Qualification snapshots the
profile and canonical service configuration; only browser fields may differ.
It retains the frozen cases, observes both adapters' actual executable images,
and rejects profile/receipt or executable mismatches. These checks establish
launch identity. Doe ownership still requires the Chromium lane's actual page
compute/presentation, lifecycle and missing-C negative controls; a workspace run
or matching command line alone does not qualify C GPU execution.

Scripted comparisons and both scripted differentiation suites forward the same
profile to treatment and control, retain their frozen headless mode, and require
the selected image to match the OS-observed images. Both also support installed
`--browser-channel` selection. The billed model-driven suite retains its own
frozen browser and rejects these overrides; no profile is silently ignored.

Schema 3 stock-browser configurations remain readable. To select a Fawn build,
start with the schema 4 canonical config; protocol 3 and existing journal records
are unchanged. Existing stock-browser evidence retains its recorded scope.

For the focused dispatcher-to-GPU development control, run:

```bash
node test/browser/gpu-composition.js --browser-config /absolute/new-c-profile.json \
  --out /absolute/new-evidence-directory
```

This uses both real adapters: an authorized click starts ordinary page WGSL
compute and a canvas triangle; the host independently checks all eight numeric
outputs and all 64 RGBA pixels. It requires a reported physical adapter, completed
queue work, canvas frames, cleanup, and a durable completed action after service
restart without resubmission. It binds the fixture, runner, oracle, selected files
and OS-observed browser images; C mode additionally requires the selected runtime
to be observed mapped in the browser process tree. Failed runs stop the sequence
and retain their errors. This is a small composition control, separate from the
frozen behavioral qualification, Chromium missing-C/lifetime gates and application
performance campaigns. It does not establish general browser compatibility or a
product advantage.

The `2026-10-26-behavioral-v3-platform-exits` case freeze retains the same25 cases,
102 runs, fixtures and semantic expectations. Crash evidence now records the
acknowledged controller kill or synchronously received self-kill request separately
from the raw Node close-event tuple. Linux/macOS report signal SIGKILL/code null;
Windows reports that tuple for an acknowledged controller kill, and signal null/code1
for a synchronously received self-kill request. The validator rejects normal exits
and wrong termination codes or missing kill requests rather than fabricating a
POSIX signal on Windows.

## Known limits

- One page per workspace; popups and additional tabs are only counted.
- The outline names a select control by its option text, not its label.
- Page scope treats any DOM change as a page change, so continuously changing
  pages need `regions` scope with the regions an action depends on.
- Chromium answers no page evaluation while a navigation is pending, so
  observations and probes wait for it to commit, bounded by
  `dispatch.actionTimeoutMs`.
- One process holds a journal; the journal is not compacted, and observation
  history is kept for the current owner generation.
