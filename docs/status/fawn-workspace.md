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
account-scoped persistence and trusted host controller integration. Its
[process tests](../../browser/workspace/test/unit/workflow-store.test.js) and
[synthetic browser check](../../browser/workspace/test/browser/workflow-store.test.js)
exercise publication and isolation; they do not promote live-site support.

## Active blockers

- Workflow reuse remains a programmatic API. UI integration, live ATS account/review
  verifiers, variable-length loops and learning from repairs remain unimplemented
  or unqualified. The personal store requires trusted task-specific verifiers.
- The product client has development and bounded live-planner evidence, not
  general task superiority or release admission. Its matched investigation probe
  completed on both arms; Fawn did not establish an advantage.
- The separate frozen model-driven differentiation campaign has not run. The
  small product probe does not satisfy it or its held-out application coverage.
- Multi-page behavior has local browser checks; the new protocol needs its own
  cross-platform qualification and external retention evidence.
- [Integrated proof](../thesis.md#three-proofs) still requires a source-matched
  forced-Doe browser and complete-application correctness/performance admission.
  The configured external Chromium source checkout is unavailable. Retained
  browser diagnostic runs and passing workloads do not qualify that build or
  establish the required aggregate performance advantage.

Workspace evidence, [GPU integration](../browser-lane.md) and
[hosted artwork](fawn-start.md) have separate authority and acceptance.

[Previous detailed status and historical evidence routing](https://github.com/clocksmith/fawn/blob/c8834c7ddb3bdbb7bc52f21a12bdb74221c80753/docs/status/fawn-workspace.md).
