# Bounded trace prototype

`src/client/trace-dag.js` is an opt-in session API above stock Chromium. It is
not connected to the HTTP/UI command surface. The caller
selects a task template, supplies current input slots, authorizes intermediate
clicks and explicitly starts recording or replay. It cannot acquire ownership or
grant permission. Existing service credentials and grants are required.

`src/client/workflow-agent.js` composes this cache with the bounded planner loop.
`runWorkflow` supports explicit `learn`, `fresh` and `reuse` modes. Learning
records the planner's slot-bound actions and publishes only after the caller's
independent review verifier accepts the actual output. Reuse can hand a structural
miss or a proven never-dispatched stale action to the planner. Authorization
failure and uncertainty stop the run; neither causes renewed grants or a retry.
Both fresh and fallback planning use the same tools and action checks.
Every invocation creates a fresh client run, including cache-only replay. The
planner's `finish` proposal leaves it running while the independent review runs.
The client persists completion only after review acceptance and a new context,
authority, cancellation and outstanding-action check. Rejected or unavailable
review records `needs-attention`; cancellation records `stopped`. A superseded
run cannot overwrite its replacement. These fences do not make website changes
and verification atomic or reconcile an unknown effect.
Direct `runWorkflow` callers opt into repair collection with `learnRepairs: true`;
the personal store enables it by default for installed, independently verified
tasks. Frozen comparison callers leave it off. Fresh mode never collects.

```js
const dag = new TraceDag({ maxNodes: 64, maxEdges: 64, maxSteps: 12 });
const options = {
  service, owner, pageId: 'main', intent: 'form', template: 'review-form-v1',
  inputs: { name: currentProfile.name },
  intermediateClicks: [['button', 'Continue', 'button', '']],
};
const session = dag.session(options);
await session.record({
  kind: 'fill', locator: ['textbox', 'Full name', 'input', 'text'], slot: 'name',
});
// Record the remaining approved intermediate steps, check the pre-submit result,
// then publish this completed trace. finish() never submits the form.
await session.finish();
const result = await dag.session({ ...options, inputs: nextProfile }).replay();
```

## Recording and replay

Recording captures a successful action and its following live observation on
write. It stages edges until `finish()` confirms the trace is complete. Failed,
uncertain, interrupted and over-capacity traces leave the cache unchanged.
Limits count nodes, edges and actions; graph publication is all-or-nothing.

A node combines the task template, exact origin, read scope, action prefix and
ordered structural outline. The prefix distinguishes successive fills even when
the outline stays unchanged and makes the graph acyclic. Branches can have the
same action and different resulting outlines; an unseen result stops after that
action. Multiple possible actions stop before dispatch. Terminal nodes stop even
if another recorded trace continues from them. This is conservative reuse, not a
branch-predicting planner.

Service ownership is not website account identity. This prototype does not check
the logged-in account, tenant or session identity; the caller must restrict cache
reuse accordingly. Matching origin and structure does not prove account
equivalence. Fixed repeated steps can be unrolled into a trace, but variable-length
loops remain unsupported. Explicit repair learning is described below; safe
replanning and uncertain-effect reconciliation remain distinct operations.

The opt-in [personal store](workflow-store.md) adds a required account namespace,
task-specific account checks and pre-dispatch slot disclosure rules. Scoped
sessions append `accountScope` to node identity; unscoped sessions retain their
existing fingerprints. Prefix order is unchanged. The host must provide real
account/review verifiers; the synthetic fixture supplies only its own oracle.

Each step re-observes through the service, requires one structural target and
binds its live handle, selector, digest and observation version. Admission,
dispatch authority and value settling remain in the existing service. Unknown or
pending actions block a new trace; a failed step stops its session permanently.
Replay does not reconcile, retry, authorize or submit anything on its own.

## Verified repair suffixes

Replay stages the actual successful actions and their observed result nodes,
including an action whose resulting structure misses the old edge. On a
structural miss, `session.repair()` returns a new recorder carrying that ordered
prefix, action depth and staged records. It re-observes and refuses if the last
recorded result no longer matches. It cannot resume an uncertain, unauthorized,
failed, cancelled or stale session. No prefix actions are executed again.

For a scoped group miss, the recorder adds a same-depth bridge from the stopped
group node to an exact-page node and records the remainder without group-depth
assumptions. A later replay computes that exact outline from its fresh observation
before following the bridge. The account, origin, read scope and action prefix
still participate; every action still checks current slot disclosure and service
authority. This is narrow reuse of a reviewed variant, not fuzzy field inference.
Several exact variants can coexist; conflicting actions remain a safe miss.

The independent task oracle must accept the whole result before `finish()`.
Failed review, uncertainty or interruption leaves the shared graph unchanged.
`finish({publish: false})` stages a terminal for the personal store; the store
persists execution accounting before merging all records and bridges under its
cross-process lock. A publication limit or I/O error therefore does not erase
the verified execution result. The raw trace API's default finish still publishes
to its in-memory DAG. No graph is derived automatically from journal files.

Only fill, select and explicitly allowed intermediate clicks are supported.
Every fill/select references a supplied input slot; even a fixed option must be
a declared slot. Literal values in recording requests are refused. Native submit,
file, password and hidden inputs are excluded. A custom button's business effect
cannot be inferred from its role: the trusted task author must identify which
clicks are intermediate. A model-provided click allowlist is not authorization.

## Observation and storage format

Element observations add `role`, `label` and `inputType`. Labels use visible
referenced text, ARIA labels or associated labels, with text fallback for a few
interactive roles. This bounded reader is not Chromium's full accessible-name
algorithm. Label/order/type changes cause misses. An unnamed target or truncated
observation is refused. Values, instance IDs, descriptions and requisition URLs
are absent from the structural outline; the full content hash still governs
service admission. These additive fields are returned live and do not migrate
the service journal or its existing observation authority.

`dag.export()` returns generated version-2 state:
`{version, key, limits, nodes: [[nodeHash, {depth, edges, terminal, repairs}]]}`.
Each edge is `{step: {kind, locator, slot}, next}`; `locator`, `slot` and `next`
are keyed fingerprints (`slot` is null for clicks). Raw labels, input values,
URLs, selectors, handles and owner credentials are never copied into the graph.
The cache is still private state, not an anonymized public artifact: its key and
structure permit correlations and candidate matching. Callers persisting an
export must use private storage; do not add it to the public repository.

`TraceDag.restore(state, limits)` validates version, bounds, edge shape and
acyclic depth relationships. `repairs` lists exact-page targets at the same action
depth; these targets cannot themselves have repair bridges. Bridges count against
the edge limit. Version-1 nodes restore with an empty repair list; their
fingerprints are unchanged. Old store snapshots remain immutable, while new
exports use version 2. Older clients must be upgraded before reading them.
Restore never resumes a session.

`record(spec, observation)` optionally accepts the exact service observation
shown to the planner. Its action binds that version; it cannot silently replace
a stale planner decision with a fresh target. Existing callers omitting the
observation retain the explicit session API.

## Development checks

```sh
node browser/workspace/test/unit/trace-dag.test.js
node browser/workspace/test/unit/workflow-repair.test.js
node browser/workspace/test/unit/workflow-completion.test.js
node browser/workspace/test/browser/trace-dag.test.js
node browser/workspace/test/browser/workflow-repair.test.js
```

The browser check records four steps in a synthetic multi-step ATS-style form,
round-trips the private cache through disk, then replays another requisition with
different IDs, page text, URL and profile values. Both Playwright and raw CDP use
the real service. Assertions cover no planner calls during replay, no submissions,
unseen branches, ambiguous locators, missing slots, literal rejection, click
allowlists, revoked grants and ownership changes. Unit checks add incomplete and
uncertain traces, cancellation, conflicting paths and resource bounds.

This proves neither live Greenhouse/Ashby coverage nor a performance advantage
over competent stock automation. Loading a cache does not enable replay in the
product client. UI integration remains separate. The [Sonnet comparison](qualification/workflow-reuse.md)
measures learning, reuse and safe fallback against fresh planning on a frozen
synthetic application; it does not establish live ATS support.
