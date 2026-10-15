# Fawn workspace qualification, scripted comparison and agent-workflow differentiation

**Consumers:** `fawnWorkspace.qualificationEvidence` and
`fawnWorkspace.differentiationEvidence` in
[the strategy projection](../../../config/doe-product-strategy.json), enforced by
[`bench/tests/test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py).
These results replace the earlier record for commit `973a8fb29`. That record
remains in Git history at commit `0e1135b92`, under
`reports/fawn-workspace/20261014-behavioral-qualification/`.

## Contents

- [`qualification/`](qualification/): the full behavioral run of case set
  `2026-10-14-behavioral-v2` and the [validator's verdict](qualification/verdict.json).
  Raw journals and fixture logs for every run are under `runs/`.
- [`comparison/`](comparison/): the scripted comparison against competent stock
  Playwright automation, rerun on the same commit.
- [`differentiation/differentiation.json`](differentiation/differentiation.json):
  the full run of the differentiation suite frozen at commit `02239e6dc`. It holds
  per-run rows, the workbench's mutation logs and orders, the requirement
  evaluations and the product-advantage rule.

## Source identity

All three results ran commit `59b95f9ce7eb98a93ddd3d6d8e4e14f5af2aa685` with no
working-tree changes under `browser/workspace` or `config`. Each records:
- source and harness tree digests
- config and package-lock hashes
- Node and `playwright-core` versions, and the operating system and kernel

Every adapter, side and arm ran the same Chrome for Testing headless shell
executable, identified by SHA-256 and version from `/proc`. No browser process
held a listening TCP socket or a DevTools port file.

Re-validate the behavioral result with:

```bash
cd browser/workspace
node qualification/validate.js ../../reports/fawn-workspace/20261015-agent-differentiation/qualification --require-current-source
```

## Limits

- The scope is the frozen case set and suites on the recorded browser binary and
  this Linux host, not release qualification or compatibility beyond the
  fixtures.
- Scripted workflows show no material advantage over competent stock automation.
- Agent-workflow advantage is measured against Playwright agent tooling under
  frozen semantics: page-wide incremental snapshots, aria-ref actions, and a
  pause that resumes the pending call after human control. The agent is a
  deterministic policy, not a language model. A control that revalidates every
  decision, discards pending calls on takeover or scopes reads with
  site-specific locators would narrow the gaps.
- The service adds latency to every task. Clean and churn runs sit just inside
  the frozen latency rule.
