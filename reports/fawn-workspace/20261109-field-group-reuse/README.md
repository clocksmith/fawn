# Bounded field-group reuse decision

[Decision and evidence bindings](decision.json) admit opt-in reuse of reviewed
field groups. They do **not** establish superiority over authored automation.
The [frozen method](../../../browser/workspace/qualification/workflow-generalization/README.md)
defines the contact projections, independent field-ID oracle and separate stock
control. One learning posting and a different held-out posting were captured per
vendor. The complete original vendor pages were not reproduced.

All 170 case executions passed their expected completion or safe-stop behavior,
with zero observed wrong-field fills and zero submissions. That total includes
controlled preflight, learning and stock-control rows; only 36 rows used live
Sonnet, including four learning runs. All 158 real calls reported
`claude-sonnet-5-5`; provider-reported cost was $0.772863.

The table shows complete-task seconds, including preparation, across the two
vendors. Exact per-vendor rows and model counts are in the decision. Stock
control used matched inputs and Chrome 157.0.8103.38 in a separate run, so this
is not an interleaved performance qualification.

| Local variant | Whole-page reuse | Scoped reuse | Stock script |
| --- | --- | --- | --- |
| Held-out contact form | 2.51–2.54 s; 0 calls | 2.39–2.66 s; 0 calls | 2.03–2.28 s |
| Extra question | 31.16–35.86 s; 9 calls | 15.84–16.18 s; 5 calls | 2.35–2.37 s |
| Reordered controls | 24.44–31.21 s; 8 calls | 2.36–2.43 s; 0 calls | 2.03–2.04 s |
| Reviewed label change | 27.03–28.49 s; 8 calls | 2.36–2.38 s; 0 calls | 2.03–2.04 s |

Scoped reuse filled five of five requested fields on reordered and reviewed-label
variants, and four of six on the added-question variant before returning to the
planner. It did not cross an unreviewed dependency boundary. Ambiguous and hidden
duplicates, account mismatch, denied disclosure, revocation and uncertainty
stopped; stale never-dispatched actions could be freshly planned.

The initial trial is retained as rejected: concurrent service edits violated
source integrity, and the duplicate-control fixture used the wrong input type
for Lever. The corrected, refrozen fixture passed from isolated source. No
acceptance threshold was relaxed.

The stock script was faster on these normal variants. Human script setup and
maintenance remain unmeasured. A participant volunteered, but this record
includes no completed human result. Repaired suffix learning is also absent:
the local repeat instrument deliberately measures whether the same rescue is
needed again. These remain product-value gaps rather than implied wins.
