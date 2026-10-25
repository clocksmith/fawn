# Native-direct mapping and identity qualification

Consumer: regression qualification of the existing direct addon boundary after
`feddd469` (mapping access) and `b7c6a5dd` (queried identity). The accompanying
JSON retains source/provider hashes, queried limits, exact captures and all
96 timed samples. This is **not a promoted performance claim**, a replacement
for the default-wrapper negative receipts, or physical GPU qualification.

## Repairs and unchanged controls

All three unchanged package plans initially failed because direct readback
first requested a mutable native pointer for a READ mapping. C correctly
logged the rejection; the supervisor correctly rejected stderr. The addon now
selects the getter by mapping access, and copy-only helpers request const data.
The supervisor and data oracles are unchanged. The new integration control
checks READ/WRITE, mapped-at-creation, copies, discarded READ mutations,
detachment, and empty stderr on both C and retained Zig.

The direct adapter's duplicate info implementation hardcoded a nonfallback
classification. It now shares the full addon's info owner. The executor reads
its existing `getInfo()` method, preserving query failures rather than
recording null identity. Both providers report CPU fallback true; their
reported subgroup bounds and limits remain distinct and are retained, not
normalized into invented equality.

## Matched diagnostic

Existing canonical executors `doe_node_native_direct` and
`node_webgpu_package`; unchanged transfer, eight-pipeline and queue plans;
16 samples plus one separate warmup per workload/provider. Dawn block first,
then C-direct: serial, not randomized. Node 24.22.1; pinned npm webgpu 0.4.0,
not the separately built Windows Dawn producer. Same scoped CPU ICD,
`LP_NUM_THREADS=1`, Mesa shader cache disabled, separate fresh HOME directories,
forced `DOE_PACKAGE_READBACK_MODE=mapAsync-host-copy`. These select the same
outward copy contract, not identical internal copies or implementation policy.
All 96 samples have zero errors, unsupported or skipped commands and retain
the original exact readback oracles.

| Workload | Host operation ms, C / Dawn | Process wall ms, C / Dawn | CPU ms, C / Dawn | Peak child KiB, C / Dawn |
|---|---:|---:|---:|---:|
| 1 MiB upload/readback | 4.581 / 3.727 | 226.552 / 211.754 | 244.158 / 226.146 | 97054 / 99562 |
| Eight pipelines | 85.418 / 82.041 | 311.048 / 297.228 | 328.997 / 416.882 | 100420 / 125354 |
| Queue completion | 15.514 / 16.847 | 239.032 / 223.704 | 254.437 / 259.225 | 98242 / 111054 |

These are medians. C-direct has lower peak individual-child RSS in all three
and lower queue host-operation time, but loses process wall in all three.
It also loses transfer and pipeline host-operation time. Host-operation time
is **not GPU latency**. RSS is GNU time's maximum individual child, not combined
simultaneous process-tree memory or VRAM. No universal superiority follows.
The full-wrapper earlier losses remain independently applicable.

## Reproduction

Use the existing `bench/cli.py run` with `--executor-id doe_node_native_direct`
(or `node_webgpu_package` for Dawn), `--iterations 16 --warmup 1`,
`--resource-probe none --resource-sample-ms 20 --resource-sample-target-count 0`.
Use the prospective CPU workload contract retained in
`c-native-dawn-package-diagnostic.json`, never an Apple/AMD promoted hardware
contract. Resolve the recorded provider, driver, addon and plan hashes before
running; set the explicit C library via `DOE_WEBGPU_LIB`. Make GNU `time`
available as `gtime` on a scoped PATH. Keep output in ignored `bench/out`.
The correctness regression is reproducible independently with:

```sh
node packages/doe-gpu/test/integration/test-integration-native-map-access.js
```

Set `DOE_C_TEST_ADAPTER=software` only when deliberately selecting a CPU ICD;
that asserts fallback classification, not physical support. Original matching
C/Zig MSAA failures, frozen compiler gates, unavailable physical/browser
qualification, and historical hygiene failures are not waived by this report.
