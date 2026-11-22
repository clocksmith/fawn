# Blitzen workspace service

The browser-agnostic shared observation, action, workflow-memory and recovery
service above stock Chrome/Chromium (`@doe/blitzen`, formerly "Fawn Workspace").
Inside [clocksmith/fawn](https://github.com/clocksmith/fawn), three layers have
distinct names and acceptance boundaries:
- **Doe** owns the native WebGPU runtime and WGSL compiler (`Dawn -> Doe`).
- **Fawn** owns the custom Chromium browser build (`Fawn.app`).
- **Blitzen** (`browser/workspace/`) owns durable browser workspaces, workflow
  reuse/repair (`TraceDag`), and investigation handoff above stock Chrome by
  default, with optional hash-pinned `Fawn` browser integration.

The service is JavaScript: an end-to-end C GPU route means selecting Doe's C
compiler/runtime in the Fawn browser, not rewriting Blitzen or Chromium code in C.
[The workspace contract](../../docs/fawn-workspace-contract.md) owns requirements
and acceptance; [status](../../docs/status/fawn-workspace.md) records what is
established (retaining historical `fawn-workspace` filenames and `fawnWorkspace`
strategy keys so frozen evidence hashes stay intact). Its behavior is qualified on the frozen case set for a recorded
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
- `src/workflows/`: document research, controlled form submission, artifact
  export and engineering incident investigation (`engineering-investigation.js`).
  They contain the task logic and reach the browser only through the
  service API.
- `qualification/`: the behavioral qualification harness and the
  competent-baseline comparison (see [Qualification](#qualification)).
- `test/`: unit tests (fake adapter, real process kills) and a real-Chromium
  qualification subset that also checks the validator rejects tampered evidence.

Tunables and protocol coverage come from
[`config/fawn-workspace-service.json`](../../config/fawn-workspace-service.json).

## Explicit file uploads

The human client can select files when delegating a task. It confirms the filenames
and allowed origins before sharing them, then clears the selection for the next
task. A site can read an attached file immediately; no final submit is necessary.
No file or site is implicitly authorized by a learned workflow.

Trusted hosts may instead pass `uploadFiles: [{ name, base64 }]` to an administrator's
`grantAuthorization`, with `upload` in `actionKinds`. The result returns opaque
`fileId`, name, byte length and SHA-256 descriptors. Agent actions use
`kind: 'upload'`, an observed native file-input target (including hidden inputs),
`params: { fileIds: [...] }`, `effect: { external: true, effectKey }`, and explicit
`completion.completedWhen`. No local path is accepted from an agent. A custom
button or drop zone needs an actual file input; an arbitrary non-file element
cannot receive bytes through this action.

The service snapshots selected bytes outside the journal, enforces configured
file/count/staging limits, rechecks authority and the snapshot hash before dispatch,
and uses Playwright or CDP without a native chooser. It first verifies the input's
filename/size list remains attached for the configured settling interval, then
evaluates the caller's completion conditions. `input-files-equal` conditions take
`selector` and `files: [{ name, byteLength }]`; that establishes attachment, not a
server receipt or business acceptance. Sites that clear the input before this
check remain unverified, even if they may already have received bytes. Missing
acknowledgments remain unknown and never authorize automatic retry.

Revocation stops queued actions, but cannot retract already disclosed bytes.
Snapshots remain available to open pages for later submission. After all browsers
close, unused/revoked snapshots are retired; live grants retain snapshots for
restart. Staging exhaustion rejects new selections rather than evicting a file
an open page could still use.

Service config schema 5 adds `uploads` limits and optional upload grants while
retaining protocol 4 and existing journal replay. Config schemas 3 and 4 still
load without uploads. The client request limit includes base64 expansion. Focused
checks: `node browser/workspace/test/unit/uploads.test.js`,
`node browser/workspace/test/browser/uploads.test.js`, and
`node browser/workspace/test/browser/product-uploads.test.js` from the repo root.
These local controlled fixtures do not qualify real-site uploads or model quality.

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
  `fill`, `select`, `submit`, `download` and explicitly authorized `upload`. A control that submits a form must
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
  When a selector names a canvas, its digest also covers the backing bitmap.
  Chrome computes SHA-256 over tiles of the selected canvas in sRGB RGBA8.
  The service journals hashes, dimensions, and tile size. Each observation can
  read up to 4,194,304 canvas pixels, with at most 256 tiles per canvas.
  An unreadable, empty, tainted, or over-budget canvas yields no digest.
  `observation.canvasBackend` defaults to `cpu`. Selecting `webgpu` copies the
  first selected raster to a renderer-owned texture, retains one prior texture
  per document and read scope, and compares them in one compute pass. Only
  changed or translucent tiles need fresh CPU pixel reads. At action dispatch
  the service independently reads the scoped pixels again on CPU. CSS overlays
  and other compositor layers fall outside this canvas-bitmap observation.
- **Composited element evidence.** An administrator can enable
  `observation.compositorCapture` on a visible Chromium browser, or launch
  Blitzen with `--compositor-transport native-helper` for the stock helper. Call
  `observe({ regions: ['#surface'], compositorRegion: '#surface', compositorSession: 'start' })`
  on a scoped workspace with a live grant naming that canvas and origin. The
  user selects the current tab in Chromium's sharing chooser. The service
  attaches its frame reader only after the browser confirms element restriction.
  Rendering includes the target's CSS. Occluding sibling elements are excluded.
  Reuse `frameEvidence['#surface'].sessionId` as `compositorSession` for later
  observations. A write citing that observation rechecks the current grant and
  reads a fresh restricted frame on the same stream. Each sample closes its
  frames. Ending sharing in the browser, replacing the element, navigating,
  revoking the grant, handing off, or losing contact with the service retires
  the stream. A new share requires an explicit start and browser consent. Only
  the evidence and session identifier survive a restart. `frameEvidence` records the selected
  surface ID, document loader, grant revision, media timestamp and captured
  dimensions. Rectangles in this mode use captured-frame pixels.

  For GPU capture, select `--compositor-digest webgpu` or set
  `observation.compositorDigest` to `webgpu`. The default `cpu` maps the
  changed tiles and hashes their packed RGBA bytes with Web Crypto.
  The WebGPU mode runs the same SHA-256 over each changed rectangle in its
  resident texture. Each selected tile uploads a 16-byte job and maps a
  32-byte digest. The comparison mask has its own readback. Both modes rehash
  the full selected frame before a write. Both conservatively revisit
  translucent tiles. A GPU digest failure stops the observation. The receipt
  reports mapped tile bytes, their digest or pixel payload, job uploads and
  compute passes. Use `--compositor-transport external-texture` to avoid the
  explicit host upload used by `rgba-upload`.


  `observation.compositorTransport` selects `external-texture` (direct WebGPU
  import), the default `rgba-upload` (WebCodecs RGBA read into host memory
  and upload), or `native-helper`. The helper sends the granted element’s
  RGBA pixels over the private CDP pipe to a workspace-owned Node worker.
  The worker shares the service’s OS account and receives pixel bytes and
  dimensions. Browser bindings and write credentials stay with Blitzen.
  Native buffer comparison skips unchanged hashes; OpenSSL hashes changed
  tiles and rehashes all pixels before dispatch. The worker retains one prior
  frame per active share and ends on revocation or missed renewal. The journal
  stores frame identity and tile digests. The receipt counts the raw
  RGBA copy, base64 payload, helper input, hashed bytes, and maximum combined
  size of its previous and current pixel buffers. The investigation client's `capture` accepts the same
  `compositorRegion`, saves a digest-bound evidence record, and reuses its
  selected region when refreshing that record. A changed rendered surface can
  therefore revise the evidence while the page text stays identical.
  Transport failures stop the observation. A region-scoped action citing this
  evidence recaptures the surface, reads every canonical pixel and refuses a
  new document, replacement element, revised grant or changed digest. An
  application revision check can still cover the interval between revalidation
  and an external write. With a live renderer and a disconnected service, an
  eight-second heartbeat lease closes the share when browser timers run.
  A region-scoped action can bind the digest to detect a repaint. Applications
  with interchangeable cells also need a stable entity and revision at dispatch.
  Targetable elements are links, buttons, inputs, selects, textareas and elements
  with an ARIA widget role (button, link, checkbox, radio, switch, combobox,
  listbox, option, menu items, tab, textbox, searchbox, spinbutton, slider).
  The outline shows a combobox or listbox's current choice and the `checked`,
  `selected` and `expanded` states.
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
  as `unchanged`. Changed canvases include a `pixels.changed` list of
  rectangles in intrinsic bitmap coordinates. In the default full view,
  `visualChanges[selector].changed` provides the same rectangles alongside
  current action handles. A missing or incompatible baseline covers the whole
  current canvas with `baseline: 'new'`. Consumers should use the version
  returned with those rectangles when proposing an action on the same surface.
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
- **Values and choices.** Without caller completion conditions, a `fill` is
  verified with `input-value-equals` and a `select` with `choice-equals`. The
  service polls until the page shows the value, then requires it to stay for
  `dispatch.valueSettleMs`. A value never shown is `unknown` with basis
  `value-unverified`; one shown and then lost, for example to late hydration,
  is `unknown` with basis `value-reverted`. `select` takes an option's value or
  label. It sets a native `<select>` directly. For an ARIA `combobox` it opens
  the popup when collapsed, then clicks the one visible option with that name
  among the elements the combobox names in `aria-controls` or `aria-owns`, or
  anywhere when it names none. For a `listbox` it clicks the option inside it.
  `choice-equals` reads a native select's selected option, a combobox's own
  value or text, or a listbox's single `aria-selected` option. A widget that
  shows its choice elsewhere (some libraries render it beside an empty input)
  stays `value-unverified` unless the caller supplies a completion condition
  that reads where the page shows it.
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
  differentiation suites in `workspaceComparison` and the engineering-investigation suite:
  - `differentiation`: a procurement workbench (`workbench-site.js`)
  - `revalidatedDifferentiation`: a helpdesk (`helpdesk-site.js`) against a
    stronger control that revalidates decisions before mutating and cancels
    pending calls on human takeover
  - `engineeringInvestigation`: an incident queue, runbook and postmortem suite
    (`investigation-site.js`, [`config/fawn-engineering-investigation-suite.json`](../../config/fawn-engineering-investigation-suite.json))
    against the same revalidating, takeover-cancelling control

  Each agent has a core (`fawn-agent.js`, `playwright-agent.js`) and a
  per-application profile (`procurement-tools.js`, `helpdesk-tools.js`,
  `investigation-tools.js`).
  - Workload: multipage agent workflows on a live site, judged by that site's
    own records.
  - Agent: one shared policy per application (`agents/procurement-policy.js`,
    `agents/helpdesk-policy.js`, `agents/investigation-policy.js`) decides on
    both sides with the same think time.
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
npm run differentiate  # first suite; --suite revalidatedDifferentiation or --suite engineeringInvestigation runs the others
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

For the selected compositor surface check, run the following from
`browser/workspace` in a disposable, visible browser session. Select the
current **Blitzen ephemeral compositor fixture** tab when prompted. Choose
`rgba-upload` or `external-texture` explicitly. To exercise a pinned Fawn
executable, set `BLITZEN_TEST_BROWSER_CONFIG` to its browser profile.

```bash
BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_TEST_COMPOSITOR_TRANSPORT=external-texture \
  node test/browser/compositor-canvas.test.js
```

`BLITZEN_COMPOSITOR_RECEIPT_PREFIX` optionally writes a new JSON receipt for
each adapter. Each includes actual planner-call count and reported model spend,
along with permission requests, consumed frames, local application HTTP requests
and explicit host-copy, GPU tile-mask and padded tile-readback bytes. The optional
`BLITZEN_TEST_NATIVE_STOP_SHARING` test helper must operate only a disposable
test display and click Chromium’s own Stop sharing control. The fixture verifies
the native track ended and the pre-write check refused the old grant. Use
`BLITZEN_TEST_CAPTURE_LEASE=1` to exercise service-heartbeat expiry. The
separate `test/browser/compositor-service-crash.test.js` uses the same visible
browser opt-in and kills the fixture-owned service after its first capture.
To exercise the application write and reconciliation for the same granted
compositor surface, run `test/browser/canvas-review.test.js` with
`BLITZEN_TEST_ELEMENT_CAPTURE=1` and `BLITZEN_REVIEW_COMPOSITOR_TRANSPORT`
set to the transport under comparison. `BLITZEN_RESIDENT_RECEIPT_FILE` selects
a new receipt file. The clock begins when the fixture requests a repaint and
ends after the authoritative record confirms the interrupted write. The
native-helper control measures CPU-native work outside the browser. A native
GPU helper and hardware-level copy counts need independent instrumentation.
A software Vulkan run checks behavior. Native backend selection and transfer
ownership still require the corresponding Chromium lane identity and trace.

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

## Human workspace client

Normal launches of **Blitzen** use installed Google Chrome (`--browser-channel chrome`) by default,
storing workspace state under `~/.local/state/blitzen`. To run Blitzen against a
hash-pinned **Fawn** browser build instead (`--browser-channel fawn` or
`--browser-config`), create a private profile with
[`qualification/create-browser-profile.js`](qualification/create-browser-profile.js)
selecting the actual executable image (on macOS, `Contents/MacOS/Chromium-real`),
the explicit `doe-c` or `dawn` runtime, and the build's dependency files. When
`fawn` or `--browser-config` is requested, missing or changed artifacts stop
startup with zero fallback to Chrome.

```bash
# Default: launch Blitzen on installed Google Chrome:
npm --prefix browser/workspace start
# Optional explicit planner configuration:
BLITZEN_MODEL=<selected-model> ANTHROPIC_API_KEY=<credential> npm --prefix browser/workspace start
# Explicit hash-pinned Fawn browser build (no fallback):
npm --prefix browser/workspace start -- --browser-config /absolute/browser-profile.json
```

Open the private loopback login link printed by the command. The browser remains
the work surface; **Compact view** opens a companion control window. Create an
investigation, browse its pages, keep source evidence, then delegate only the
selected pages, regions, origins and actions. Saved evidence/notes require separate
disclosure consent. Take over at any time; already dispatched effects may still
complete. **Check result** reads evidence and never repeats an action.

A resumed delegation receives prior attempts, human notes, open questions and
unresolved actions. Handoff pages are bounded; `handoff_read` follows the opaque
cursor in `pagination[collection].nextCursor` to older entries. The total
`unresolvedActionCount` includes actions omitted from the displayed page. A
model's finish proposal cannot complete a run while an action remains unresolved.

Saved evidence stays intact in the journal. Planner context includes a bounded
recent view; `artifact_list` searches and pages through all explicitly shared
records, so long investigations do not eagerly reload every source. `artifact_read` retrieves a bounded range from an explicitly
shared artifact at `expectedRevision`, with source and excerpt hashes. It does
not refresh the source or add filesystem or page permissions. Revising a
conclusion preserves `needs-review` while any supporting premise is unreviewed.

Malformed action proposals can return to the planner only when admission attests
that they were never dispatched and the error is correctable. Rejected proposals
and corrections consume the ordinary request budget. Unknown effects, denied
authority and service failures still stop the run; they are not automatic retries.

State defaults to `~/.local/state/blitzen` (`BLITZEN_STATE_DIR`; legacy
`FAWN_WORKSPACE_STATE_DIR` is also accepted); use `--state-dir` to select another
directory. Admin credentials stay in its private `client-credentials.json`.
`--port`, `--adapter playwright|cdp` and `--headless` select local presentation.
`BLITZEN_BROWSER_CHANNEL` (`chrome` default; `chromium`, `chromium-headless-shell`,
or `fawn`) selects a browser channel, while `BLITZEN_BROWSER_CONFIG` selects a
pinned Fawn profile (`FAWN_BROWSER_*` aliases remain supported). A Fawn profile
and a stock channel cannot be combined. Browser selection does not change
workspace authority. The planner is disabled unless both model and credential are
configured. No model runs on startup.

Protocol 4 adds page identity and opt-in scoped read grants. Earlier journals replay
with main-page compatibility. Restored tabs open blank with an explicit Reopen
control; no page navigation or pending tool is silently repeated. The product
client fences the previous operator and revokes interrupted agent-task grants
during restore. `node browser/workspace/test/browser/product-crash-recovery.test.js`
exercises a real Chromium post followed by SIGKILL before outcome persistence,
then live reconciliation without a duplicate. This development check has been
exercised on macOS Chrome; it is not a cross-platform or performance qualification.
The product client remains unqualified for general task advantage until its own complete-task
comparison passes. Prior frozen-suite results keep their original scope.

With an already authenticated Claude Code installation, select the planner-only
CLI port instead of an API key:

```bash
BLITZEN_PLANNER=claude-cli npm --prefix browser/workspace start
```

The CLI port disables built-in tools, MCP, plugins/project discovery and session
persistence. It runs in a separate empty directory, requests only structured tool
proposals, and records the actual provider-reported model and usage. Its per-turn
budget and cancellation grace are in `fawn-workspace-client.json`. The workspace
service, not the CLI, performs authorized browser actions. No CLI call starts until
a person delegates.

### Investigation comparison

The [controlled shopping comparison](qualification/shopping/README.md) composes
field-trace reuse with fresh product/quote checks and separately authorized order
submission. Its local merchant oracle and scripted control test completion,
repeatability and recovery. It has no live Best Buy adapter or Cerebras comparator.

### Move a learned routine to another workspace

In `browser/workspace`, set `BLITZEN_LOGIN_URL` privately to the login link
printed by the local Blitzen service. Take human control of the investigation.
Export selected evidence references and, optionally, a learned template.

```sh
npm run --silent handoff -- export "$SOURCE_PROJECT" handoff.json --evidence "$EVIDENCE_ID" --routine "$TEMPLATE"
```

Repeat `--evidence ID` for each saved observation, or pass `--no-evidence`.
The new file contains evidence hashes and revisions, an optional routine graph
and opaque references to outstanding source actions. URLs, page contents,
screenshots, typed values, account credentials, grants and workflow keys stay
in the originating workspace. Exchange the file through a channel you trust.
Its digest checks contents. Sender authentication belongs to the exchange channel.

With the recipient service's own login link and an existing investigation:

```sh
DIGEST=$(npm run --silent handoff -- import "$RECIPIENT_PROJECT" handoff.json)
npm run --silent handoff -- read "$RECIPIENT_PROJECT" "$DIGEST"
npm run --silent handoff -- use "$RECIPIENT_PROJECT" "$DIGEST" --template "$TEMPLATE" --page main --inputs recipient-inputs.json
```

Import saves a reviewable snapshot across restarts. Running it is a separate
human command. It requires a matching locally installed `--workflow-module`
with the same task source hashes and trusted disclosure and review definitions.
Its `verifyReadOnlyHandoffStep({ service, owner, pageId, step })` must independently
confirm the current recipient page and that this specific field only filters
local results. A fill can otherwise trigger site-side effects. As with the
account verifier, inspect current page identity without replacing the outstanding
observation used for dispatch.

Execution supports a single unambiguous sequence of fill/select steps, fresh
recipient inputs and a new recipient grant restricted to the page, origin and
required action kinds. The local verifier checks the result and the grant
expires or is revoked. Branches, clicks and repairs are available for inspection.
Graphs incorporating edits from outside the exporting investigation lack the
provenance for this export. An outstanding action stays with its source account.
Resolve it there and export a new snapshot before using the routine. Evidence
references let the recipient identify separately shared material; they cannot
open the source browser or retrieve its saved contents.

The optional [trace session prototype](trace-dag.md) records completed
parameterized paths and replays them through the service without planner calls,
stopping at an unknown branch or the pre-submit boundary. It is a programmatic
API, not automatic product-client behavior or a qualified live-site capability.

The [workflow reuse comparison](qualification/workflow-reuse.md) adds explicit
Sonnet-driven learning and safe planner fallback, with a frozen synthetic
application, independent review checks and fresh-planner controls.

The opt-in [personal workflow store](workflow-store.md) persists account-isolated
versions, verified repair suffixes and run records with cross-process publication.
Trusted host task definitions supply account/review verifiers and pre-dispatch
disclosure rules (`--workflow-module PATH` or `openWorkspaceApp({ workflowPersistence, workflowTasks })`).
When host task definitions are installed, the loopback client UI exposes explicit
workflow selection (`learn`, `reuse`, `fresh`) and slot inputs without graph
upload, secret exposure or automatic startup execution.

`node qualification/product-compare.js --out <directory>` compares the product
client with stock Chromium plus the same durable evidence/issue tracker and
planner. The policy fixes source pages, repository code, model, tool contract and
spend ceiling. `--controlled` checks the harness without model calls. The live
probe records provider-reported models/usage, complete-task and preparation wall
time, source hashes, dependency coverage, failures and the independent duplicate
charge reproduction. It is diagnostic: a single pair and mechanical source checks
establish neither semantic answer quality nor a product advantage. Historical
workflow comparisons and frozen promotion criteria remain independently required.

Private cross-client workflow storage and deployment: [shared registry](workflow-registry.md).

### Physical Resident Process comparison

`qualification/resident-comparison.py` uses the ordinary Blitzen service and the
same canvas, browser-granted element, journal, deterministic decision and
revision-checked write on every arm. It runs stock Dawn, stock with native CPU
helper, Fawn/Dawn and forced Fawn/Doe. The two Fawn profiles must pin the same
executable and dependencies. Use a reserved Linux or macOS GPU with a visible
desktop, Python 3.10+ and the browser/workspace Node dependencies installed.
The reserved run requests Chromium sandboxing on all arms and checks actual
launch flags for disabled GPU/sandbox, forced software or automated consent.
Close unrelated GPU work before running. The fixture opens its own temporary
profiles and loopback site. Approve only its current-tab chooser for each live
process. Existing browser profiles and their grants are unnecessary.

From `browser/workspace`, set `FAWN_CHROME` to the packaged browser executable
and `DOE_C_LIBRARY` to its matching Doe library. Use optional repeated
`--dependency FILE` arguments consistently for any extra pinned build members.

```sh
physical_run=$(mktemp -d)
node qualification/create-browser-profile.js --executable "$FAWN_CHROME" --runtime dawn --out "$physical_run/dawn.json"
node qualification/create-browser-profile.js --executable "$FAWN_CHROME" --runtime doe-c --library "$DOE_C_LIBRARY" --out "$physical_run/doe.json"
python3 qualification/resident-comparison.py plan --repeats 20 --concurrency 1 2 --out "$physical_run/results"
python3 qualification/resident-comparison.py run --out "$physical_run/results" --dawn "$physical_run/dawn.json" --doe "$physical_run/doe.json" --stock-channel chrome
python3 qualification/resident-comparison.py report --out "$physical_run/results"
```

Some upstream Chromium snapshots report forty zeroes for the revision in
DevTools. On Linux, pass `--source-receipts STOCK.json FAWN.json` to `run`.
Each JSON file records the verified build source (`source`), the complete
`chromium` and `dawn` revisions, `product` (e.g. `Chromium 158.0.8108.0`)
and the executable's `chromeSha256`. The runner verifies the actual browser
process belongs to the fixture and hashes its image before releasing the cohort.
The source-to-image mapping comes from the build or distribution records.
Placeholder revisions alone cannot qualify a comparison.

The offline `plan` command freezes the arm order and concurrency sweep. A repeat
rotates the first arm. At concurrency two, two independent Resident Processes
each have their own browser, page and grant. Every process waits at the same
pre-repaint barrier. If their measured work does not actually overlap, that
wave is excluded from paired latency. Run `report` on a partial directory to
list missing, refused, incorrect, and unattributed attempts alongside wins.
A new run takes a fresh output directory.

`report.json` gives the host clock bracket around the actual canvas mutation,
the browser mutation timestamp, and the host timestamp of verified reconciliation.
Per-process latency is the interval from the mutation to verification; the
host midpoint is an estimate and both bounds are retained. A compositor frame
commit timestamp is still unavailable. P95 is nearest rank. Pairing is by
repeat, concurrency and worker slot. The stock-Dawn/Fawn-Dawn pair additionally
requires matching observed browser revision and version; comparing build options
is necessary before attributing a difference to Fawn. The Fawn-Dawn/Fawn-Doe
pair isolates the selectable runtime in the identical pinned browser.

Each receipt includes the oracle tile, final application revision and effect
count, measured bytes at the copy/readback boundaries, planner calls and spend,
and sampled process-tree RSS. This is one deterministic rules workload; the zero
model calls and spend say nothing about adaptive planners. Whole-tree summed RSS
counts shared pages multiple times. On Linux, `gpuProcessDrm` samples only the
fixture Chromium GPU process after readiness. Its peak records all reported
resident memory regions from the same sample. Duplicated DRM clients count once.
Distinct clients can share buffers. A missing or inaccessible driver counter
leaves the sample unknown. Physical bus traffic and unique device-wide VRAM
remain unmeasured. [Linux DRM client accounting](https://docs.kernel.org/gpu/drm-usage-stats.html). Refusals retain reached phases and known costs; missing fields
mean unknown.

**Doe timing eligibility.** A selector, an adapter string or a page-authored
success cannot prove who handled a command. The restricted page labels its
diff command buffers with the live capture ID and per-sample sequence, and each trial binds its target
ID and capture ID. Timing eligibility requires an independent native trace of
that target's labeled submission and completion on the pinned Doe library, with
fallback excluded and the verified effect intact. The current collector cannot
produce that proof, so the report always withholds Doe timings. Raw diagnostic
timestamps remain private inputs. Backend failure or uncertain execution is a
recorded outcome.

Verify the report offline with `python3 -m unittest discover -s qualification/tests -p 'test_resident_comparison.py'`.

#### Independent native Vulkan completion

For a Linux diagnostic, `qualification/trace-doe-vulkan-queue.py` reads a
fresh child under GDB. It checks the supplied SHA-256 of the linked Doe C image
and the mapped image's device and inode at each breakpoint. It observes entry
into the actual public queue submit, a direct compute dispatch encoded by that
queue, and the matching native Vulkan command in the terminal poll or wait.
The collector emits completion only when Doe's accepted native command enters
its successful fence/timeline completion branch. A timeout, release attempt,
killed process or page-authored label supplies no such evidence. The observer
changes scheduling and its timestamps cannot be used for a speed comparison.

The focused test requires GCC, GDB with Python and the image's matching DWARF,
Bubblewrap, and Mesa Lavapipe. It runs a child in a private network namespace without
a GPU device. Two live Doe devices receive the same command label; the test
requires distinct native queue receipts and eight correct readback words.
A second child is interrupted while Doe has accepted the compute command and
the observer has yet to see its fence. Its receipt must remain unresolved.

```sh
qualification/tests/check-native-queue-observer.sh "$DOE_C_LIBRARY" "$DOE_C_SHA256" "$NEW_OUTPUT_DIR"
```

**The shipped browser at the pinned revisions.** The browser process
can validate a Blink document token and renderer process against the live
`RenderFrameHostImpl`. Its existing GPU-host reply returns the network
isolation key. Several documents share a cache partition. This reply cannot
identify the tab, grant, or isolated world that issued a WebGPU command. The
GPU decoder scopes wire handling to itself and tracks known devices; an exact
mapping from a browser-authorized target/document generation and isolated
context to the submitted wire device/queue/command is absent from these
receipts. Workers have separate token types and cannot be assigned to a tab by
matching their origin or renderer PID.
[Browser lookup](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/content/browser/gpu/gpu_process_host.cc#1132)
· [GPU decoder](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/gpu/command_buffer/service/webgpu_decoder_impl.cc#2571)

Dawn's WebGPU backend records an outer command into an inner encoder. At the
pinned revision it finishes that encoder with a null descriptor, then submits
the resulting inner handles to the selected queue. Fawn's patch changes pass
lifetimes here but does not forward the page label at inner finish. Even if
forwarded, a label is page-controlled. A native receipt needs a trusted,
browser-originated mapping plus a submission identity allocated outside the
page, carried through that outer-to-inner conversion, retired on teardown and
joined to the actual backend command's terminal result. Navigation, a changed
grant, recycled handles and cancelled work must invalidate any stale join.
The software native observer proves only the backend half; the four-arm report
continues to withhold Doe timings.
[Outer-to-inner conversion](https://dawn.googlesource.com/dawn/+/758e5e6a974e8ca8039b1d7b209646dabccbcb7f/src/dawn/native/webgpu/CommandBufferWGPU.cpp#1144)
· [Queue submission](https://dawn.googlesource.com/dawn/+/758e5e6a974e8ca8039b1d7b209646dabccbcb7f/src/dawn/native/webgpu/QueueWGPU.cpp#69)

**Opt-in Chromium source change.** Apply the normal runtime patches first. The
document-provenance patch targets Chromium `4a6fb38567da` with Fawn's pinned
Dawn `758e5e6a974e`. It changes no default command line. Use an isolated
checkout, build a symbolized browser, and preserve the sandbox for qualification.

```sh
lane=$(cd ../chromium && pwd)
"$lane/scripts/apply-runtime-patches.sh" "$CHROMIUM_SRC"
git -C "$CHROMIUM_SRC" apply --check "$lane/patches/resident-document-provenance.patch"
git -C "$CHROMIUM_SRC" apply "$lane/patches/resident-document-provenance.patch"
```

With `--fawn-resident-command-provenance` and forced Doe, a browser-validated
active document receives an opaque binding. Only the privileged browser trace
contains its DevTools frame and loader. Deactivation, frame deletion, document teardown and the frame’s last active
media stream ending retire the binding. Another active stream can keep the
aggregate media indicator on after one particular capture stops. The GPU service retains the binding for
one decoder. Single-command wire submissions mint a fresh ID at the synchronous
Dawn queue call. Multiple-command submissions and workers remain unattributed.
The native collector reads the scope only from that exact executing stack in a
separately pinned executable or library. It continues to require the compute
encoding, accepted submission and successful Doe fence.

For a private diagnostic child, supply `BLITZEN_NATIVE_TRACE_SCOPE_IMAGE` and
`BLITZEN_NATIVE_TRACE_SCOPE_SHA256` along with the existing Doe pin. Independently
collect the privileged Chrome trace with category `disabled-by-default-gpu.dawn`.
Record the matching CDP `Tracing.tracingComplete` event parameters, including its
`dataLossOccurred` flag. Start tracing before acquiring the GPU device and end the observation window after
fence completion. Use browser and GPU PIDs from the controller in the same PID
namespace. The correlator rejects ambiguous input and excludes any document
retired during the window. It expects complete controller-owned artifacts from
this run. Arbitrary JSON or a page-supplied receipt has no evidentiary value.

```sh
python3 qualification/correlate-document-submit.py --browser-trace "$BROWSER_TRACE" \
  --tracing-complete "$TRACING_COMPLETE_PARAMS" --native-log "$NATIVE_LOG" --browser-pid "$BROWSER_PID" --gpu-pid "$GPU_PID" \
  --frame "$FRAME_ID" --loader "$LOADER_ID" --scope-sha256 "$BROWSER_IMAGE_SHA256" \
  --doe-sha256 "$DOE_C_SHA256"
```

**Capture and world qualification.** For a separate correctness run, enable
`BLITZEN_RESIDENT_SCOPE_PROBE=1` in the trusted workspace service and point
`BLITZEN_RESIDENT_SCOPE_TRACE_PREFIX` at a fresh private file prefix for
`test/browser/canvas-review.test.js`. The adapter creates a fresh CDP isolated
world for the one authorized session. It records the browser-returned context
number, frame, loader, grant revision and bracket number. The world name and
visible command label carry no authority. Stock Chrome can run the full
workflow and return that scope request while emitting zero Fawn native records.

The diagnostic samples `getSettings()` on the *same live restricted track*
immediately before the diff submission and after the queue reports completion.
It leaves the selected element and track alone. The patch observes the actual
Blink track, realm and native media session. The browser independently records
the approved element restriction, the version and any later change or close
of that media session. Trace payloads hash the native media and target
capabilities. The correlator rejects a second track in the same world, changed
restrictions, ambiguous command batches and missing fences.

At the real Blink queue submit the patch flushes earlier wire work, scopes one
command in the isolated world and joins the serializer's existing random batch
ID to the GPU decoder's received batch. The separately pinned native observer
must see its unique scoped submission complete on Doe. The ordinary web API
gains no privileges or methods. These extra flushes, witnesses, tracing and
the GDB observer change timings. Four-arm reporting excludes this diagnostic.

The fixture's CDP session writes `.trace.json`, `.complete.json` and
`.scope.json` prior to dispatch, then verifies the actual application outcome.
Pass `--scope-request "$PREFIX.scope.json"` in addition to the correlation
arguments above. In an isolated test, `BLITZEN_RESIDENT_SCOPE_LOOKALIKE=1`
requests a second share from the main world. If granted, it restricts that
stream to the same element. A refused second share is recorded and only the
same fixture canvas is captured by the independent control. In both cases the
main world submits real GPU work using the copied label and stops its stream. The original grant must still serve its own verified action. An
ordinary page cannot manufacture the native trace category using a user
timing mark.

The native software test compiles the exact scoped C++ submit function,
shimming tracing and the anti-tail-call operation. Synthetic trace tests cover
separate documents and worlds, concurrent streams, a changed target, missing
fences, closure and forged labels. The full positive capture-to-fence result
requires an instrumented, sandboxed Chromium build emitting these events on
the same run as the independent GPU collector. A workspace receipt, stock
browser completion or synthetic trace cannot substitute for that evidence.

**One-command Linux capture attribution.** On a private desktop, select the
fixture tab when Chromium asks to share it. Use the same pinned Fawn/Doe
workspace profile used for the four-arm comparison. The executable and Doe
library need debug symbols for the optional Chromium patch and native observer.
In a component build, pass `--scope-image` with the path of the pinned
dependency containing `FawnSubmitResidentDocumentCommandV1`. The pinned Linux
component build links that function into `libgpu_gles2.so`.

```sh
cd browser/workspace
python3 qualification/run-capture-attribution.py preflight --profile "$FAWN_PROFILE" --out "$FRESH_PREFLIGHT"
python3 qualification/run-capture-attribution.py run --profile "$FAWN_PROFILE" --out "$FRESH_DIAGNOSTIC" --timeout 300
```

Preflight verifies the images and debug symbols without opening a browser.
The run starts the existing Blitzen canvas fixture in a fresh profile. Its
private browser control pipe identifies the GPU child. The controller checks
child ancestry, executable, start time, forced runtime and GPU sandbox before
arming the native fence observer. The browser chooser still authorizes the
selected region. Use `--lookalike` for the separate copied-label fault control.
A policy that prevents examining the fresh child produces a refusal. Keep the
normal browser and OS sandbox active.

A fresh output directory holds `result.json`, the browser trace, the native
events and the independently verified application outcome when available.
Only a matching run, capture world, live grant, queue submission and fence
with one verified effect can pass. Every result excludes timing and hardware
performance qualification. The timeout includes preflight and the chooser.

**Qualify compositor digest modes.** From `browser/workspace`, run
`node test/browser/gpu-tile-sha256.js` on a WebGPU-enabled stock browser
to check both readers against packed-row Node SHA-256. To compare through a
verified write, give the private fixture a fresh output directory and approve
its restricted share once for each browser. The comparison checks the exact
reviewed tile hashes, localized target and reconciled application revision.

```sh
out=$(mktemp -d)
for mode in cpu webgpu; do
  BLITZEN_TEST_ELEMENT_CAPTURE=1 BLITZEN_REVIEW_COMPOSITOR_TRANSPORT=external-texture \
    BLITZEN_TEST_COMPOSITOR_DIGEST="$mode" BLITZEN_RESIDENT_PROGRESS_FILE="$out/$mode.progress.json" \
    BLITZEN_RESIDENT_RECEIPT_FILE="$out/$mode.json" node test/browser/canvas-review.test.js || break
done
node qualification/compare-compositor-digests.js "$out/cpu.json" "$out/webgpu.json"
```

For a separately authorized four-arm run, `resident-comparison.py run
--compositor-digest webgpu` selects the GPU digest on the three browser GPU
arms and keeps the native helper's CPU path.

## Reviewed regional clicks

On a browser build with Fawn's native input commands, the local authenticated workspace API accepts `reviewClickTarget` with a `pageId` and the selector of an interactive canvas. The current human owner grants that region and completes the normal browser capture chooser. The response includes the exact RGBA image, its pixel dimensions, its SHA-256 digest, a target handle, and an observation version. Display those returned pixels for review. They are verified against the restricted capture before disclosure.

After approval, call `clickReviewedTarget` with the same page, handle, observation version, and a fresh action ID. The click is at the center of the reviewed rectangle. Declare the external effect and its application completion condition for a write. A browser refusal means no input was admitted. A disconnected acknowledgment leaves the outcome unknown and must be reconciled. A new owner takes a fresh capture. Stock Chromium does not implement the two native commands.

An element-only grant requires exact pixel equality. Video capture can change colors during conversion. A human who intends to review everything visibly painted inside a rectangle, including overlays, can instead pass the `visibleRectangle` from `selectRegion` to `reviewClickTarget`. This separate approval binds the selector, document, URL and rectangle. Agent credentials alone cannot authorize it. The returned `pixelScope` records which permission was used.

Run `node qualification/reviewed-drawing.js stock` or `node qualification/reviewed-drawing.js stock-helper` from this directory for the disposable drawing comparison. The `fawn-dawn` and `fawn-doe` arms require their respective hash-pinned browser profiles as the next argument. The comparison records a pixel-only edit and a separate application revision change. Every arm must recover under the recipient account, reject a lookalike document, and deduplicate a repeated commit. Timings from this correctness run are diagnostic.

A refused capture returns `review-capture-refused` with the browser's reason and `retryable: true`. No earlier approval survives it; review the region again. After capturing, the adapter waits briefly for the viewport to settle and captures again if starting tab sharing moved the page to a new compositor surface.

### Review inbox

The inbox holds the few things an agent or a person needs someone to look at. The operator invites each person with `invitePerson` (`name`, `permissions` of `review` and `finish`) and gives them the returned link; each person signs in at `/inbox` and acts only as themselves. `proposeReview` files one item: a page, the selector of an interactive canvas, a title, the proposed change, a completion condition and an optional assignee. The browser captures the region at that moment, and that image is what reviewers approve.

Each item shows the exact captured pixels, who approved them, handoff notes, and a status that comes from the browser's own check of the reviewed pixels: `still-holds` or `needs-fresh-look` with a reason. `approve` covers the current image only. `handoff` passes the item to another person with a note. `finish` needs the finish permission and an approval of the current image, and clicks the reviewed target through the browser's reviewed click, which admits it only while that image still holds. `lookAgain` takes a fresh capture; earlier approvals stay in the history and stop counting. Several items can be open on one page, each in its own review slot.

Run `node test/browser/review-inbox.test.js` with `FAWN_REVIEWED_INPUT_PROFILE` set for the two-person qualification on a busy approvals page. It records browser deliveries apart from application commits. The inbox currently requires a canvas target, like every reviewed regional click here; the browser commands themselves accept any rectangle.
