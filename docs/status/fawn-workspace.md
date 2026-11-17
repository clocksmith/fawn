# Fawn durable workspace

The [workspace contract](../fawn-workspace-contract.md) owns authority,
observation/action binding and recovery. The [product client](../../browser/workspace/README.md#human-workspace-client)
adds explicit delegation, human takeover, scoped multi-page access and connected
investigation records above stock Chromium. Planning remains outside the trusted
dispatcher. Restart returns control to the human without starting model calls.

Protocol 4 gives pages separate identities and permissions. Legacy journals and
omitted `pageId` inputs remain readable through the main-page compatibility path.
Service config schema 5 adds explicitly selected file uploads, bounded staging and
site-bound file grants without changing journal replay. The
[local upload checks](../../browser/workspace/README.md#explicit-file-uploads)
cover both adapters and human disclosure confirmation; real-site acceptance and
cross-platform upload qualification remain open.
The [implementation decision](../../reports/fawn-workspace/20261109-product-client/decision.json)
binds current development checks, local qualification and rejected promotion.
Its raw development evidence stays local; the projection is not a portable
replacement for semantic revalidation.

## Promoted boundary

The [strategy contract](../../config/doe-product-strategy.json) still binds the
previously admitted [platform evidence](../../reports/fawn-workspace/20261026-platforms/README.md)
and frozen differentiation suites to their recorded sources and controls. Their
scoped advantage concerns outcome ownership across interruption and scoped
observation. Scripted workflows show no material overall advantage over competent
stock automation. None of that evidence transfers automatically to the new client,
protocol, browser build or platform.

The opt-in [workflow API](../../browser/workspace/trace-dag.md) has
[matched Sonnet reuse evidence](../../reports/fawn-workspace/20261109-workflow-reuse/decision.json)
on a frozen synthetic application. The diagnostic establishes verified learning,
parameterized replay and safe fallback within that fixture. It preserves the
initial failed trial and all paired results; it does not promote the broader
product or establish superiority over scripted automation.

The opt-in [personal store](../../browser/workspace/workflow-store.md) adds local
account-isolated persistence, verified changed-form repair suffixes and explicit
loopback UI delegation for host-installed task definitions. Its
[process tests](../../browser/workspace/test/unit/workflow-store.test.js),
[synthetic browser check](../../browser/workspace/test/browser/workflow-store.test.js),
[separate-process repair check](../../browser/workspace/test/browser/workflow-repair.test.js)
and [UI workflow check](../../browser/workspace/test/browser/product-workflow-ui.test.js)
exercise publication, repair reuse and secret containment on synthetic forms.
[`engineering-investigation.js`](../../browser/workspace/src/workflows/engineering-investigation.js)
([unit check](../../browser/workspace/test/unit/engineering-investigation.test.js))
and the [`engineeringInvestigation`](../../config/fawn-engineering-investigation-suite.json)
differentiation suite cover incident evidence capture, foreign-origin skipping,
precondition guards, postmortem export and interruption recovery against the
revalidating Playwright control. In addition, the
[held-out and live-site runner](../../browser/workspace/qualification/held-out-and-live-investigations.js)
([browser check](../../browser/workspace/test/browser/held-out-investigations.test.js),
[diagnostic receipt](../../reports/fawn-workspace/20261110-heldout-and-live-investigations/decision.json))
evaluates `WorkspaceService` on stock Chrome against stock Chrome plus a local
tracker across three held-out local investigations (multi-page cache bleed with
ungranted-origin rejection, mid-run human takeover with stale-generation
rejection, and interrupted failover POST reconciliation with zero duplicate
commits versus one duplicate on unguarded retry) and two read-only live public
HTTPS investigations (`w3.org` + `developer.mozilla.org` with live
ungranted-origin rejection, and `rfc-editor.org` paired with local code
evidence), strictly separate from Doe GPU and Fawn browser runtime claims.
Pre-indexing DOM selector uniqueness in `page-query.js` and separating
ungranted-origin probe time from task timing (`d6e8fa805`) brought live
`completeTaskMs` to `3724.85ms` versus `2674.55ms` on `w3.org` + `developer.mozilla.org`
(`delegatedMs`: `694.21ms` vs `499.18ms`) and `1660.09ms` versus `1457.16ms` on
`rfc-editor.org` (`delegatedMs`: `239.92ms` vs `392.69ms`). On read-only
multi-page capture without interruption or authority handoff, stock Chrome plus
a local tracker still completes in less total wall-clock time than
`WorkspaceService` navigation setup and mediation (`productReleaseQualified:
false`). Together with the controlled [shopping comparison](../../browser/workspace/qualification/shopping/README.md),
these checks do not establish live-site merchant or ATS mutation support.

A [real-investigation handoff and reuse diagnostic](../../reports/fawn-workspace/20261116-handoff-and-reuse/decision.json)
runs one engineering investigation through a lost tracker acknowledgement, a
crash, a scripted human restart and a different model resuming
([`real-handoff.js`](../../browser/workspace/qualification/real-handoff.js)).
Fawn and stock Chrome plus a tracker passed every fixed acceptance check in all
six pairs with zero duplicate posts. At `052b5b0c23` Fawn used more planner
calls (21, 17, 21 versus 14, 15, 15), cost more and took longer in all three
pairs. At `bf865f931f` a resumed session checks each unknown action's recorded
completion condition on the live page before the planner runs, and the planner
view marks form-submitting controls; Fawn then used 12 calls versus 15 to 16,
about $2.26 versus $3.09 and 87 to 95 s versus 114 to 138 s in all three pairs.
That advantage was a product default, not something stock automation cannot
do: the stock control had no automatic outcome check. Given the same check
(`73de12f7a2`, `--stock-outcome-check`), both arms passed with 12 planner calls
at the same cost in all three pairs, and stock finished sooner in two of them
(mean 72.6 s versus 84.2 s). No advantage over that control is established on
this investigation. The tracker is a local fixture and the human step is
scripted. The same record binds an uncontaminated
[product workflow reuse run](../../browser/workspace/qualification/workflow-reuse.md#unmodified-product-revisions):
at `052b5b0c23`, 5 planner calls versus 18 for fresh planning with all 18 cases
correct but no elapsed-time advantage, because one browser shutdown stalled for
300 s. After the release bound (`d0f4680434`) one clean run completed in 25.8 s
with 5 calls versus 70.1 s with 17 for fresh planning; an earlier run at that
revision is retained as invalid because a planner timeout failed a control row.
The prepared stock script finished in 3.8 s with no model calls, so reuse does
not beat a script.

## Active blockers

- Stock Chrome is slow to exit after a graceful close once it has run for about
  30 s: 14 s alone and 586 s with several instances on one machine, on a blank
  page with a fresh profile, and Playwright waits without a limit. That stalled
  the reuse run (300 s in the Fawn arm, 7.7 to 24.2 s in four stock rows). The
  Playwright adapter now [bounds the release](../../browser/workspace/src/adapters/bounded-close.js)
  at the action timeout and then ends that profile's browser (16 s in the same
  586 s conditions, no process left). The release reports what it established:
  `graceful`, `forced` or `already-exited` only when no browser remains on the
  profile, and `unresolved`, with the surviving process ids and the failed
  step, when exit could not be shown; the adapter keeps owning an unresolved
  browser and the next handoff says so. Why Chrome is slow to exit is not
  established, a forced exit can lose the profile's last unflushed site data,
  and the CDP adapter and Windows are not covered. The clean reuse run after
  the bound had no stall, so only the probe exercises it. A
  [real-Chromium check](../../browser/workspace/test/browser/forced-release-durability.test.js)
  ends the browser with no time to exit and restarts: the journal bytes and
  every recorded action survive and the profile reopens. A session cookie set
  just before the release was lost in two of three runs; the workspace then
  shows the site as signed out. The product client records a forced release
  and the next handoff warns that sign-ins may be lost.
- Sign-in belongs to a person. Observations report a password field only as
  empty or `[hidden]`, the service refuses to fill one for any owner (admitted
  values are journaled), and a delegated agent that proposes it is stopped for
  attention. A [real-Chromium check](../../browser/workspace/test/browser/sign-in-handback.test.js)
  on a local signed-in fixture loses the session across a forced release, has
  the agent ask for sign-in, has a person type the password at the browser and
  a later agent finish, with the password absent from observations, planner
  inputs and both journals. Planners are scripted there. No real external
  account, live model, one-time code, single sign-on redirect or headless
  deployment (where a person cannot reach the browser window) is exercised.

- Personal workflow learning, verified repair-suffix reuse and UI delegation
  require host-installed task definitions and local verifiers. Live ATS and
  merchant account/review verifiers, variable-length loops and open-web workflow
  generalization remain unimplemented and unqualified.
- The product client has development and live-planner probe evidence, not
  general task superiority or release admission. Its matched investigation probe
  completed on both arms; Fawn did not establish an advantage.
- The separate frozen model-driven differentiation campaign has not run. The
  product probe does not satisfy it or its held-out application coverage.
- Multi-page behavior has local browser checks; the new protocol needs its own
  cross-platform qualification and external retention evidence.
- [Integrated proof](../thesis.md#three-proofs) still requires direct Doe wire
  ownership in Chromium and complete-application correctness/performance
  admission over Dawn. Retained diagnostic runs and passing workloads do not
  qualify a release browser or establish the aggregate performance win.

Workspace evidence, [GPU integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) have separate authority and acceptance.

[Previous detailed status and historical evidence routing](https://github.com/clocksmith/fawn/blob/c8834c7ddb3bdbb7bc52f21a12bdb74221c80753/docs/status/fawn-workspace.md).
