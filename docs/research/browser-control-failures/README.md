# Browser-control failure catalogue

[catalogue.json](catalogue.json) collects 78 failure cases from 164 sources across
16 categories. The owner requested collection from local Codex, Claude and Gemini
histories and the internet on November 9, 2026. This is research for choosing
reproductions, not a change to Fawn's goals or evidence that Fawn solves them.

The cases comprise:

- 12 cases from individual public issue reports;
- 45 failure classes from a second public pass over benchmark error analyses,
  vendor limitation documentation, automation-tool documentation and issues
  (21 rest on benchmark analyses, 21 on issues and 3 on documentation alone);
- 19 cases drawn from inspected local tool outputs, 11 of them added by a second
  local pass;
- 2 cases resting on an agent's own report: a Gemini browser-subagent note
  corroborated by an exact error string, and an agent's account of a second
  session sharing its browser.

Multiple sources can describe one case; one source can describe several. These
counts are not incident frequency or failure rates.

## What was found

| Category | What fails | Cases |
| --- | --- | --- |
| `connection-lifecycle` | The agent loses or cannot reach its control connection | 8 |
| `window-discovery` | The control tool cannot find or keep its window or element binding | 2 |
| `launch-configuration` | The browser does not start as requested | 5 |
| `target-identity` | The action reaches a different tab or element than intended | 3 |
| `ownership-concurrency` | More than one controller or person drives the same browser | 3 |
| `input-routing` | Input is intercepted, mis-addressed or never delivered | 6 |
| `control-state` | The page does not keep the value or choice the action set | 8 |
| `observation-freshness` | The agent reads an out-of-date page state | 4 |
| `action-scope` | An action does more or less than the requested operation | 3 |
| `outcome-verification` | Success and failure reports disagree with what happened | 5 |
| `observation-coverage` | The agent cannot see content or controls on the page | 10 |
| `native-surface` | A dialog, picker or prompt outside the page blocks the task | 5 |
| `page-topology` | Tabs, frames, navigations and downloads move work out of view | 5 |
| `control-permission` | The control tool is not authorized to drive the browser | 1 |
| `access-control` | Login, verification or bot walls stop the task (blockers only) | 3 |
| `agent-reasoning` | The model plans or judges wrongly; the browser can supply evidence only | 7 |

Case IDs for each category are in [catalogue.json](catalogue.json).

The local records add three patterns. Losing the control connection or the
window binding recurs across tasks and dates. The native file dialog's confirm
button stayed disabled for a valid file across separate batches, sites and two
browsers. A custom combobox showed a different option than the one requested. No
local record shows an agent claiming false success; agents flagged unverified
saves and submissions instead.

The public classes add layers the first pass had not covered: observation gaps
(shadow DOM, canvas-rendered applications, snapshot and screenshot fidelity),
browser-owned surfaces (JavaScript dialogs, file choosers, select popups and
browser prompts), page topology (new tabs, cross-origin frames, downloads and
navigation policy), access walls, and model-side failures (grounding, loops,
unverified completion, prompt injection). Model-side classes are kept so a
comparison can separate them; for those the browser can only supply evidence.

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
- Public sources from the second pass carry `excerpts`: the quoted text or
  reported figure, keyed to the case that cites it. Their `kind` is
  `public-issue`, `benchmark-analysis`, `paper`, `vendor-doc` or `tool-doc`.
- `layerAssessment.fixableInBrowser` is the public pass's judgment of whether a
  browser or control layer could detect or fix the class. It is untested.
- `local-agent-message` sources are an agent's own narration or ledger text,
  not tool results.
- `related-development-check` means a Fawn development test exercises a synthetic
  version of the mechanism, named in `fawnChecks`. It does not reproduce the
  cited reports and is not a resolution.

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

A second local pass on the same day extended this to 109 Codex session files and
their 109 compressed archives (the archives held no primary records), the Codex
log database, 25 Claude project folders and about 120 Gemini conversation
databases. A second public pass collected 45 classes. Quotes were matched against
the retrieved text, title-only citations are marked, and the state of every cited
GitHub issue was read on the collection date. Published rates of models
attempting CAPTCHA bypass were deliberately left out.

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

Two cases first included on a supplied progress narrative were withdrawn when
relayed corrections disputed the events. The second local pass found the original
tool results for both: the disabled confirm button in the native file dialog
(November 3 and November 9, in Chrome and Safari) and the combobox that showed
"option Yes, selected" after setValue "No" (November 9). They return as
`BF-local-native-file-dialog-confirm-disabled` and
`BF-local-combobox-choice-not-retained` on that tool evidence. The private source
map records which relayed corrections the records contradicted. Repetition in a
summary still does not corroborate an event, and the older local examples remain
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
