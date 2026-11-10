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
evaluates the workspace on stock Chrome across three held-out local investigations
(multi-page cache bleed with ungranted-origin rejection, mid-run human takeover
with stale-generation rejection, and interrupted failover POST reconciliation
with zero duplicate commits versus one duplicate on unguarded retry) and two
read-only live public HTTPS investigations (`w3.org` + `developer.mozilla.org`
with live ungranted-origin rejection, and `rfc-editor.org` paired with local code
evidence), strictly separate from Doe GPU claims. On read-only multi-page capture
without interruption or authority handoff, stock Chrome plus a local tracker
completes in less wall-clock time than `WorkspaceService` mediation. Together with
the controlled [shopping comparison](../../browser/workspace/qualification/shopping/README.md),
these checks do not establish live-site merchant or ATS mutation support.

## Active blockers

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
