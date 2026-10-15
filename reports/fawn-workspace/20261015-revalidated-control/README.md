# Fawn workspace qualification, compatibility, and both differentiation suites

**Consumers:** `fawnWorkspace` in
[the strategy projection](../../../config/doe-product-strategy.json), enforced by
[`bench/tests/test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py):
- `qualificationEvidence` binds the behavioral verdict
- `differentiationEvidence` binds the first suite
- `strongerControlEvidence` binds the second suite
- `compatibilityEvidence` binds the Chrome run

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
- [`comparison/`](comparison/): the scripted comparison against competent stock
  Playwright automation.
- [`differentiation/differentiation.json`](differentiation/differentiation.json):
  the first suite, a procurement workbench against Playwright agent tooling.
- [`revalidated-differentiation/differentiation.json`](revalidated-differentiation/differentiation.json):
  the second suite, frozen at commit `a9686d47e`. It runs a helpdesk against
  Playwright agent tooling that revalidates every decision before mutating and
  cancels pending calls on human takeover.

## Source identity

Every result ran commit `9f522f7d98406eca21a2db5062d76c917c375c47` with no
working-tree changes under `browser/workspace` or `config`. Each records:
- source and harness tree digests
- config and package-lock hashes
- Node and `playwright-core` versions, and the operating system and kernel
- the browser executable actually running, identified by SHA-256 and version
  from `/proc`

No browser process held a listening TCP socket or a DevTools port file.

Re-validate the behavioral results with:

```bash
cd browser/workspace
node qualification/validate.js ../../reports/fawn-workspace/20261015-revalidated-control/qualification --require-current-source
node qualification/validate.js ../../reports/fawn-workspace/20261015-revalidated-control/compatibility-chrome
```

## Limits

- Behavior is qualified on two Chromium builds on this Linux host, not on other
  engines, operating systems or headed sessions, and is not release qualification.
- Scripted workflows show no material advantage over competent stock automation.
- Against the stronger control, gaps from changing decision inputs and human
  handoff close. The advantages that remain are durable outcomes across agent
  interruption and region-scoped incremental observation.
- Both suites use deterministic policies, not a model-driven agent; that stage
  needs its own freeze.
- Both suites' latency stays inside the frozen rule.
