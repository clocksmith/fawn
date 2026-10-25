# C package versus Dawn: CPU-Vulkan diagnostic rejection

Consumer: the non-CSL C migration performance handoff and regression baseline
for explicit-provider startup. This source-bound record **rejects a blanket
C-over-Dawn performance claim**. It is not promoted GPU qualification.

The [sanitized receipt](c-native-dawn-package-diagnostic.json) retains all 384
timed samples from four rounds, including the original negative results,
exact plan/provider/driver hashes, output hashes, execution counters and phase
measurements. Twenty-four additional invocations were warmups. Original raw
receipt hashes bind the selected fields; local paths and hostnames are omitted.
No original oracle, tolerance or claim threshold changed.

## Controls and interpretation

- Node 24.22.1, the same Mesa llvmpipe LLVM 19.1.7 CPU Vulkan ICD, one llvmpipe
  thread, Mesa disk cache disabled, separate initially empty provider HOME
  directories. Filesystem caches were not purged: these are fresh-process,
  warm-filesystem observations, not guaranteed cold-storage measurements.
- Unchanged queue completion, eight-kernel creation and exact 1 MiB readback
  plans. All sampled errors, unsupported and skipped counts are zero.
- Each workload/provider uses 16 timed samples and one separate-process warmup.
  First round: C then Dawn, requested `mapAsync`, internally different copy
  paths. Later rounds: Dawn then C, forced `mapAsync-host-copy`, both report
  `mapped-range-host-copy`. Provider blocks are not randomized/interleaved;
  do not infer statistical causation from round-to-round timing changes.
- No prepared session, resident input or dispatch prewarm. Existing prepared
  mode does not execute an equal warm-up dispatch on both providers and is not
  evidence of warmed GPU throughput.
- Operation timing is host wall setup/encoding/submit-wait, **not GPU timestamps**.
  Process CPU/wall include the executor lifecycle. Only the final `memory-cost`
  round has GNU-time maximum individual-child RSS. Earlier RSS sampled the
  supervisor only and cannot support total-memory comparisons. Neither metric
  measures simultaneous aggregate tree memory or VRAM.
- Actual default device limits match in 35 fields; C additionally reports
  immediate-size 64 versus Dawn 0, unused by these plans. Adapter maxima differ
  by provider policy. The comparator is npm `webgpu@0.4.0`, not the separately
  qualified Windows source-built Dawn producer.

Final-round medians (C / Dawn):

| Workload | Operation host ms | Process wall ms | Peak individual child KiB |
| --- | ---: | ---: | ---: |
| Exact 1 MiB transfer | 6.23 / 3.52 | 248.47 / 218.70 | 101160 / 99524 |
| Eight pipelines | 98.60 / 81.35 | 361.80 / 300.65 | 108598 / 125506 |
| Queue completion | 19.33 / 18.40 | 259.89 / 240.76 | 106486 / 110790 |

C loses these operation/wall comparisons. Its lower pipeline CPU and some
memory measurements do not cancel those losses or establish overall superiority.

## Reproduction with existing owners

Provide the exact hashed native libraries/addon and npm distribution identified
in the receipt. Do not run npm install scripts for this diagnostic. Resolve the
pinned Dawn package through the executor's existing
`bench/vendor/node-webgpu-package` route. Materialize `workloadContract` from the
receipt into an ignored JSON file; it deliberately identifies CPU Vulkan and
sets `claimEligible=false`, rather than reusing Apple/AMD promoted identities.
Generate the unchanged transfer asset through
`bench.lib.synthetic_assets.ensure_plan_assets`; its complete SHA256 oracle is
`77ddbb33d6d9ea7633eb24d312009bd35f6cffc0e716e09a9cb6a6ee4e367f01`.

Set scoped `DOE_WEBGPU_LIB`, `VK_ICD_FILENAMES`, `DOE_BENCH_ASSET_CACHE_DIR`,
`DOE_BACKEND=vulkan`, `LP_NUM_THREADS=1`, `MESA_SHADER_CACHE_DISABLE=true`,
`DOE_PACKAGE_READBACK_MODE=mapAsync-host-copy`, writable separate `HOME`, and
`NODE_OPTIONS=--max-old-space-size=256`. Put the verified GNU-time `gtime` on
scoped PATH for the final memory round. From the repository root, run Dawn,
then Doe, substituting the corresponding executor below:

```sh
python3 bench/cli.py run --product dawn --executor-id node_webgpu_package \
  --workloads "$CPU_DIAGNOSTIC_CONTRACT" --iterations 16 --warmup 1 \
  --resource-probe none --resource-sample-ms 20 \
  --resource-sample-target-count 0 --out "$DAWN_OUTPUT"
python3 bench/cli.py run --product doe --executor-id doe_node_webgpu \
  --workloads "$CPU_DIAGNOSTIC_CONTRACT" --iterations 16 --warmup 1 \
  --resource-probe none --resource-sample-ms 20 \
  --resource-sample-target-count 0 --out "$DOE_OUTPUT"
```

The recorded campaign additionally bounded each provider command to 120 seconds
wall/90 seconds CPU with owned-process-group cleanup. Resource sample count is
not a timeout. Inspect terminal execution counters and output oracles: executor
exit zero alone does not prove successful work.

## Changes and remaining boundaries

`3a0ff050709d71bf5f46b4693a74c7a0127ede62` materializes real prototype-accessor
adapter/device limits instead of silently recording Dawn limits as `{}`.
`03e8a2ac0539cf777011c6522b2a1fdfd355c129` checks an explicit provider before
workspace discovery: two unnecessary Git subprocesses become zero. Missing
explicit providers still fail closed; default discovery remains unchanged.
Import cost is separate and was not eliminated. A shared-wrapper-accessor
experiment regressed its micro-profile and was discarded, not published.

Retained Zig and C separately passed these three unchanged correctness plans.
That is not all-feature parity. The original matched MSAA/scissor failure,
older compiler gates and historical publication hygiene failures remain visible.
Physical GPU latency/throughput, actual Fawn browser execution, energy and
broader reliability are unqualified here. CSL remains preserved but quarantined.
