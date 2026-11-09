# Browser-control failure catalogue

[catalogue.json](catalogue.json) collects 21 failure cases from 24 sources across
10 categories. The owner requested collection from local Codex, Claude and Gemini
histories and the internet on November 9, 2026. This is research for choosing
reproductions, not a change to Fawn's goals or evidence that Fawn solves them.

The cases comprise 12 public-report cases, eight cases drawn from inspected local
tool outputs, one Gemini browser-subagent report corroborated by an exact error
string in its conversation payload. Multiple sources can describe one case; one source can describe two
different failures. These counts are not incident frequency or failure rates.

## What was found

| Boundary | Examples | Cases |
| --- | --- | --- |
| Control connection | Host unavailable; relay cannot reconnect; idle heartbeat tears down session; target closes | `BF-local-host-unavailable`, `BF-relay-reconnect`, `BF-heartbeat-session-loss`, `BF-local-target-closed` |
| Window discovery | Computer-use server returns `noWindowsAvailable` | `BF-local-no-windows` |
| Launch configuration | Missing browser binary; requested channel ignored; missing display; Linux-only mode on macOS | `BF-local-browser-binary-missing`, `BF-browser-selection-dropped`, `BF-headful-display-loss`, `BF-local-gemini-platform-refusal` |
| Target identity | Human interaction loses the intended tab; selectors match multiple controls | `BF-human-tab-drift`, `BF-local-ambiguous-locator` |
| Ownership | A second client disconnects the first | `BF-second-client-takeover` |
| Input routing | Drawers or other elements intercept click/tap input | `BF-local-occluded-target` |
| Control state | Disabled select; selected value reverts | `BF-local-disabled-select`, `BF-reactive-dropdown-reset`, `BF-dropdown-backend-regression` |
| Observation freshness | Cached text disagrees with current page; navigation destroys evaluation context | `BF-stale-content-after-action`, `BF-local-navigation-context-loss` |
| Action scope | Upload fallback clicks a submit target; text fill becomes Enter/Tab input | `BF-upload-click-fallback`, `BF-fill-control-characters` |
| Outcome verification | Upload tool reports success despite an invalid attachment | `BF-upload-false-success` |

The useful distinction is between a rejected action and an incorrect success.
An ambiguous selector or a disabled control should be refused. A timeout there
does not prove a browser bug. A success response followed by a reverted value or
invalid attachment needs a different test: inspect the resulting application
state, and where appropriate the bytes or values received by a local server.

There are existing fixes to learn from. The [browser-use dropdown report](https://github.com/browser-use/browser-use/issues/3415)
includes a maintainer's event-sequence change and the original reporter's
confirmation. The [Playwright reconnect issue](https://github.com/microsoft/playwright/issues/41874)
is closed and links a closing PR. Neither belongs in a list of proven current
competitor defects without testing a current release.

The [invalid-upload report](https://github.com/ChromeDevTools/chrome-devtools-mcp/issues/2944)
has a different outcome: the maintainer closed it as not planned and explicitly
placed application-state verification on the caller, noting that the browser
and MCP server can have different filesystems. The catalogue retains that
response alongside the report, rather than treating closure as a fix.

## Reading the records

- `sources` retain public URLs and upstream state as checked on the collection
  date, or a local source fingerprint and minimal error signature. `observedAt`
  is null when the original observation date is unknown. Collection date is not
  substituted for incident date.
- `cases` separate the observation from the root-cause assessment. A reporter's
  diagnosis is attributed, not promoted to an independently established cause.
- `evidenceLevel` distinguishes local tool output, local agent report,
  and upstream report. A shell-run browser test is labeled
  as such; it is not counted as a direct computer-use agent failure.
- `fawn.candidateOwner` maps a possible investigation to an existing source file.
  It is an inference, not an implementation assignment or coverage claim.
- `reproduction` describes a proposed setup and pass condition. All 21 are
  `proposed-not-run`; none is an implemented regression fixture yet.
- `resolution` describes what this collection establishes. Upstream issue state
  is separately retained under its source. Closed, fixed, reproduced and verified
  in Fawn are different states.

No selected observation establishes a Doe GPU cause. Start at the named control,
application or launch boundary. Attribute a GPU opportunity only after a trace
places the failure there. The [workspace status](../../status/fawn-workspace.md)
and [workspace contract](../../fawn-workspace-contract.md) retain their existing
authority; this catalogue does not promote their acceptance.

## Collection and privacy

The local discovery pass scanned 53 Codex session JSONL files and 235 Claude
project JSONL files for error signatures, then inspected selected tool outputs.
It also searched readable Gemini browser notes/history/log candidates and exact
error bytes across 60,525 conversation steps in local SQLite stores. Gemini
binary payloads were not fully decoded. Compressed Codex archives were not
searched. This is a bounded, non-exhaustive sample.

Search hits include copied text, historical summaries, test source, web results,
forked history and this collection's own searches. Those are not independent
incidents. Selected examples exclude research echoes; repeated underlying calls
are not counted twice. Category counts describe this catalogue only.

Exact source paths, record line numbers/call IDs and Gemini database row locations
are retained in the private owner research repository. Public fingerprints allow
an authorized reviewer with that private map to identify the original evidence.
Raw histories remain in their original locations. Another machine cannot verify
these private examples from this public catalogue alone.

Tracked files omit credentials, cookies, personal answers, employer/application
identifiers, private URLs and copied conversations. Generic error strings alone
are retained.

Two previously included cases based solely on a supplied progress narrative were
withdrawn after owner corrections disputed the underlying events. They are not
part of the current case or source counts. Conflicting agent-written records are
kept private for reconciliation against original tool results; repetition in a
summary does not corroborate an event. The older local examples above remain
historical examples, not evidence that separate recent task batches were one run.

## Adding a case or turning one into a test

1. Retain a direct tool result or a primary report; record its version and date
   when available. Check whether upstream fixed it, rejected it or marked it a
   duplicate. Keep that disposition with the report.
2. Check for an existing case and repeated source before adding another count.
   Use stable case IDs; group corroborating examples without inventing frequency.
3. Separate the symptom, causal hypothesis and evidence needed to distinguish
   alternatives. Name a candidate owner and an independent pass condition.
4. Reproduce on a controlled page with synthetic data. Measure resulting values,
   received files and submission counts, not just successful tool responses.
   Use a current competent stock automation control with the same model policy,
   retries and permissions when measuring a Fawn advantage.
5. Record the actual versions and results, including failures, before changing
   reproduction status. A new fixture belongs in the owning workspace tests,
   with the existing qualification rules; do not make this research JSON a
   competing gate or silently add cases to a frozen campaign.

No model calls, browser actions, submissions or runtime changes were made to
produce this collection. Local logs were read, public reports were retrieved,
and this catalogue was assembled and checked.
