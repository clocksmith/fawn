# Fawn workspace qualification on current service source

**Consumer:** `fawnWorkspace.qualificationEvidence` in
[the strategy projection](../../../config/doe-product-strategy.json), enforced by
[`bench/tests/test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py).
Steps 1–4 bind to this verdict.

The service changed after the qualification recorded in
[`20261015-revalidated-control`](../20261015-revalidated-control/README.md): service
config `2026-10-15-workspace-service-v3.3` adds the `url-not-equals` condition, and
the outline view reports each requested region group's members. This run
requalifies the changed service; it changes no step label and broadens no claim.

## Contents

- [`qualification/`](qualification/): the full behavioral run of case set
  `2026-10-14-behavioral-v2` on Playwright's Chrome for Testing headless shell,
  through both adapters and all three workflows, with the
  [validator's verdict](qualification/verdict.json): admitted, full, no failures.

## Source identity

The run used commit `a1f8e4573361c7e81c61571ac46d95e842649bd4`, with no
working-tree changes under `browser/workspace` or `config`. It records the same
identity fields as earlier runs: source and harness tree digests, config and
package-lock hashes, Node and `playwright-core` versions, the operating system,
and the browser executable actually running, read from `/proc`.

Re-validate the evidence (case set, fixture, journals, effects and adapter
agreement) on any later tree:

```bash
cd browser/workspace
node qualification/validate.js ../../reports/fawn-workspace/20261015-service-v3-3/qualification
```

`--require-current-source` also compares the service source, service config,
package lock and the whole `qualification/` directory with the recorded digests,
so it admits the run only at the recorded commit. The model-driven runner under
`qualification/model-agent/` changed after it; check out the commit to run it:

```bash
REPO=$(git rev-parse --show-toplevel)
git worktree add /tmp/fawn-a1f8e4573 a1f8e4573
cd /tmp/fawn-a1f8e4573/browser/workspace && npm ci
node qualification/validate.js "$REPO/reports/fawn-workspace/20261015-service-v3-3/qualification" --require-current-source
```

## Limits

- Behavior is qualified on the recorded headless Chromium binary on this Linux
  host. This is not release qualification.
- Step 5, the scripted comparison and the Chrome stable compatibility runs stay
  bound to their own commits in `20261015-revalidated-control`.
