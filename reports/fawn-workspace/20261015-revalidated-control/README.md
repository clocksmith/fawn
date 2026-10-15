# Fawn workspace qualification, compatibility, and both differentiation suites

**Consumers:** `fawnWorkspace` in
[the strategy projection](../../../config/doe-product-strategy.json), enforced by
[`bench/tests/test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py):
- `qualificationEvidence` binds the scripted comparison (`comparisonSourceCommit`)
- `differentiationEvidence` binds the first suite
- `strongerControlEvidence` binds the second suite
- `compatibilityEvidence` binds the Chrome runs

The behavioral verdict in `qualification/` qualifies the service source these
suites ran. Steps 1–4 now bind to the requalification of the changed service in
[`20261015-service-v3-3`](../20261015-service-v3-3/README.md).

These results replace the record for commit `59b95f9ce`. That record remains in
Git history at commit `5d2eb189e`, under
`reports/fawn-workspace/20261015-agent-differentiation/`.

## Contents

- [`qualification/`](qualification/): the full behavioral run of case set
  `2026-10-14-behavioral-v2` on Playwright's Chrome for Testing headless shell,
  with the [validator's verdict](qualification/verdict.json).
- [`compatibility-chrome/`](compatibility-chrome/): the same frozen case set on
  installed Google Chrome stable, through both adapters
  ([verdict](compatibility-chrome/verdict.json)).
- [`compatibility-chrome-headed/`](compatibility-chrome-headed/): the same frozen
  case set on Google Chrome stable in a headed session on a virtual display
  (Xvfb), through both adapters, on commit `581a16510`
  ([verdict](compatibility-chrome-headed/verdict.json)). This is headed
  compatibility evidence only.
- [`comparison/`](comparison/): the scripted comparison against competent stock
  Playwright automation.
- [`differentiation/differentiation.json`](differentiation/differentiation.json):
  the first suite, a procurement workbench against Playwright agent tooling.
- [`revalidated-differentiation/differentiation.json`](revalidated-differentiation/differentiation.json):
  the second suite, frozen at commit `a9686d47e`. It runs a helpdesk against
  Playwright agent tooling that revalidates every decision before mutating and
  cancels pending calls on human takeover.

## Source identity

Every result except the headed run ran commit
`9f522f7d98406eca21a2db5062d76c917c375c47`; the headed run used
`581a16510ab7d0bd87497adb5194f7ebce21ad4a`, which adds the launch-failure fix and
the headed override. Neither had working-tree changes under `browser/workspace`
or `config`. Each records:
- source and harness tree digests
- config and package-lock hashes
- Node and `playwright-core` versions, and the operating system and kernel
- the browser executable actually running, identified by SHA-256 and version
  from `/proc`

No browser process held a listening TCP socket or a DevTools port file.

Re-validate the behavioral results with:

```bash
cd browser/workspace
node qualification/validate.js ../../reports/fawn-workspace/20261015-revalidated-control/qualification
node qualification/validate.js ../../reports/fawn-workspace/20261015-revalidated-control/compatibility-chrome
```

`--require-current-source` admits them only on a checkout of the commit each ran,
because it compares the source, config and harness digests with the current tree.

## Limits

- Behavior is qualified on two Chromium builds on this Linux host, headless and,
  for Chrome stable, headed on a virtual display. Other engines, operating
  systems and physical-display sessions are untested. None of this is release
  qualification.
- Scripted workflows show no material advantage over competent stock automation.
- Against the stronger control, gaps from changing decision inputs and human
  handoff close. The advantages that remain are durable outcomes across agent
  interruption and region-scoped incremental observation.
- Both suites use deterministic policies, not a model-driven agent; that stage
  needs its own freeze.
- Both suites' latency stays inside the frozen rule.
