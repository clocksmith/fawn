# Fawn workspace behavioral qualification and baseline comparison

**Consumer:** `fawnWorkspace.qualificationEvidence` in
[the strategy projection](../../../config/doe-product-strategy.json). The binding
is enforced by
[`bench/tests/test_fawn_workspace_strategy.py`](../../../bench/tests/test_fawn_workspace_strategy.py):
every requirement of a step assessed `established-on-frozen-case-set` must be
established in this verdict.

## Contents

- [`qualification/`](qualification/): `result.json` and the
  [validator's verdict](qualification/verdict.json) for the frozen case set
  `2026-10-14-behavioral-v2`. `runs/<case>/<workflow>/<adapter>/` holds each
  run's raw journal and the fixture site's request log and receipts. Corrupted
  journals that fail by design are kept where the case requires them.
- [`comparison/`](comparison/): `comparison.json` with raw rows, identities and
  the evaluation under the frozen `workspaceComparison` criteria.
  `scenarios/<id>/` holds both sides' fixture logs.

## Source identity

Both results ran commit `973a8fb29a668bbec419c04180fbfa4a136770b6` with no working-tree
changes under `browser/workspace` or `config`. `result.json` and
`comparison.json` record:
- the source and harness tree digests
- the case set, service config, comparison policy and package-lock hashes
- the fixture site hash, which matches the frozen one
- Node and `playwright-core` versions, and the operating system and kernel

Both adapters and both comparison sides ran the same Chrome for Testing headless
shell executable, identified by SHA-256 and version from `/proc`. No browser
process held a listening TCP socket or a DevTools port file.

Re-validate with:

```bash
cd browser/workspace
node qualification/validate.js ../../reports/fawn-workspace/20261014-behavioral-qualification/qualification --require-current-source
```

`--require-current-source` fails once the service or harness source changes;
the result then remains evidence for its recorded commit only.

## Limits

- The scope is the frozen case set on the recorded browser binary and this Linux
  host. It is not release qualification, multi-host support or a performance
  claim.
- The fixture site is controlled and local. Real applications can differ in
  idempotency support, page activity and navigation behavior.
- The comparison does not establish product value. Success, incorrect actions and
  recovery do not differ materially from the competent control in any compared
  scenario. Time to a safe stop after a replaced target is the only material
  difference.
- Owner handoff is outside the comparison because the control has no owner
  authority.
