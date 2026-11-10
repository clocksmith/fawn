# Personal workflow persistence

`src/client/workflow-store.js` stores private, verified workflows on one POSIX host.
It is a local file store, not PostgreSQL or a cross-user recipe registry. The whole
file is sensitive: HMAC is not encryption; topology is visible and key holders can
test guessed labels and paths. Only trusted code running as the storage owner may
use this API. Namespaces do not isolate hostile code under the same OS user.

## Host integration

`openWorkspaceApp` accepts opt-in `workflowPersistence` and `workflowTasks` arguments.
Persistence requires `ownerScope`, explicit graph `limits`, `maxBytes`, `lockWaitMs`
and `lockRetryMs`. The file lives at
`<client state directory>/workflows/workflows.private.json`; the default client
directory is `~/.local/state/fawn-workspace`. Files use `0600`, the store directory
uses `0700`. Existing non-private permissions or symlinks at the file boundary
are refused. The suffix ignore rule covers exports outside this location.

Each entry in the trusted `workflowTasks` Map is keyed by template and supplies:

- `accountScope`, `verifyAccount({service, owner, pageId})`: a task-specific check
  returning the exact account scope or refusing when identity is unknown.
- `disclosurePolicy`: an exact `origin`, `fields` containing `{kind, slot, locator}`,
  and explicitly approved `intermediateClicks`. Locators are ordered
  `[role, label, tag, inputType]` tuples. Only fill/select fields are permitted.
- `verifyReview({service, owner, pageId, inputs})`, `verificationId` and
  `taskSourceHashes`: an independent review check and frozen task source hashes.
- `task` and a workflow-tool-compatible `planner`.
- Optional `fieldGroups`: reviewed ordered groups described below. They are trusted
  host definitions, never accepted from a model, graph import or HTTP body.

An explicitly requested host call
`controller.runPersonalWorkflow(projectId, {template, pageId, inputs, mode})`
returns `{runId, done}`. Await `done` for the result and persistence outcome.
The controller creates a fresh scoped grant, revokes previous agent grants, and
returns control to the human after settlement. Takeover cancels future work.
There is no workflow HTTP endpoint or UI control, graph-upload method, or
automatic workflow run on startup. Trusted host definitions cannot come from
model output or HTTP command bodies.

The lower-level `WorkflowStore.run` composes the existing workflow agent with
these checks; direct callers must supply fresh service authority. Account checks
run before observations and actions; slot-to-field rules run before every
fill/select, including the first cached fill. The service still checks the live
observation, grant and retained value. These callbacks are task-specific checks,
not an atomic guarantee against arbitrary website account changes. A matching
origin or a caller-supplied account name alone is insufficient. General ATS
account and review verifiers are not supplied.

## Verified field groups

An opt-in group is `{id, steps}`. Each nonterminal step declares
`{kind: 'fill' | 'select', slot, locators}`; its aliases are reviewed structural
locator tuples with compatible meanings. The final step is an approved
`{kind: 'click', locators}` transition. Each group admits one write per field;
groups and the learned action prefix remain ordered. This bounded form does not
support repeated writes, loops or inferred dependency graphs.

Scoped fingerprints include the entire group policy, account, origin, read scope
and ordered prefix. They normalize reviewed aliases and DOM reordering within
the current group. Missing fields miss; duplicate compatible targets, including
hidden duplicates, stop. Unrelated controls can coexist with known-field reuse,
but invalidate the transition boundary. The planner must handle that miss; the
cache cannot silently skip an extra question or a newly revealed dependency.

Every effect still resolves a unique fresh target, checks account and disclosure,
and passes the workspace service's admission and value-settling checks. Group
aliases do not widen disclosure: each permitted alias must also be listed in the
task's slot-to-field disclosure policy. Policy and source hashes bind the new
scope implementation. Existing unscoped fingerprints and receipt policy hashes
retain their behavior. Graph exports stay version 1; scoped and unscoped nodes
have disjoint fingerprints, so changing modes cannot reinterpret an old edge.

A safe miss switches to ordinary fresh planning without publishing its partial
repair. Unknown effects, ambiguity and authority failures stop. The host still
supplies an independent review oracle: structural matching cannot establish
real-site semantic correctness. The [frozen comparison](qualification/workflow-generalization/README.md)
separates that obligation from DOM read-back and comparative product claims.

## Collection and publication

The namespace is `(ownerScope, accountScope, template)`. Checkout initializes and
persists one random key per namespace. Clients restore that key rather than
create independent keys to merge later. Account scope also enters trace node
identity; ordered prefixes and existing limits remain unchanged. Unscoped legacy
sessions retain prior fingerprints. Personal persistence requires a nonempty
scope and does not import legacy exports.

Actions are staged during learning. Only a complete run accepted by the task
review oracle can submit staged records to the store's private publication
method. A repaired fallback is not published. The generated
[version-1 format](workflow-store.schema.json) has three collections:

| Collection | Meaning |
| --- | --- |
| `workflows` | Namespace hash → revision and current-version pointer |
| `workflowVersions` | Immutable snapshot, workflow ID, revision, parent version and private DAG/key |
| `workflowRuns` | Base/result version IDs, outcome, verification ID, source/policy hashes, declared and observed model identities, calls, elapsed time, intervention requests |

Receipts exclude applicant values, observations and review text. Full local
investigation journals retain their existing private behavior. Observed model
identities are recorded only when returned by the provider; an empty list is not
a provider attestation. Hashes cover persistence/execution modules and task
sources declared by the trusted host. These are development records, not
benchmark qualification.

Publication takes an exclusive `O_CREAT | O_EXCL` lock, reloads the latest state
inside it, checks namespace/key lineage and merges only verified records.
Concurrent traces retain both sets of edges; conflicting actions remain
ambiguous at replay. A changed graph creates a new immutable version and advances
the pointer. An identical graph keeps its revision. Each completed attempt adds
a run receipt in the same transaction.

The transaction writes a unique private temporary file, fsyncs it, renames it over
the current file, then fsyncs the directory. Readers see a complete old or new
state. Read-only checkout does not wait on the writer lock. Existing versions
are retained; exceeding `maxBytes` refuses the entire write without evicting them.
There is no previous personal-store format to migrate.

If saving fails, the result distinguishes verified task completion from failed
persistence (`receiptStored: false`). It never reruns browser actions to save a
cache. A post-rename durability error can leave the new state visible; reopen and
inspect rather than assuming the write failed before publication.

## Interrupted writers

Contention waits within the configured budget, then returns `workflow-store-busy`.
A killed writer may leave `workflows.lock` and a private temporary file. The store
never steals a lock based on age or a PID probe: racing to remove a dead lock can
otherwise remove a new live lock.

For explicit offline recovery, stop all store writers, check the committed file,
then remove the abandoned lock and temporary files in that directory. The
committed cache remains readable before recovery. This protocol is for a local
filesystem on one host; network filesystems need separate implementation/tests.

Optional [shared synchronization](workflow-registry.md) now adds client-side encryption
and cross-client merging. Public imports and repair candidates remain separate contracts. Same-key compatibility, graph validation and provenance never authorize
disclosure. If a future database operator is outside the trust boundary, encrypt
graph and key on the client before upload; row access rules alone do not do that.

## Focused checks

```sh
node browser/workspace/test/unit/workflow-store.test.js
node browser/workspace/test/browser/workflow-store.test.js
```

Separate-process tests cover verified learning/reuse, concurrent publication and
a writer killed after temporary-file fsync but before rename. They also exercise
failed review, unknown effects, account changes and a poisoned mapping before
its first fill. The browser check uses separate Node/Chromium processes through
`WorkspaceController`, new inputs and fresh grants; a different account misses
and falls back. Its oracle is synthetic only, with no live ATS or performance claim.
