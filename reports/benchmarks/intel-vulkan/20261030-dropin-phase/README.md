# Intel Vulkan drop-in phase baseline (B1) and candidate experiments

Consumer: proof 1 engineering attribution, as **separate backend/adapter
evidence**. The host is intel-host (Intel Tiger Lake UHD Graphics, Mesa ANV).
It is not the initial qualification target, which is the AMD Strix Halo with
RADV. Nothing here transfers to RADV, and the RADV record
([amd-vulkan/20261029-dropin-phase](../../amd-vulkan/20261029-dropin-phase/README.md))
does not transfer here. This is a **scoped native drop-in diagnostic**. It is
not the GOALS Vulkan application win and does not qualify Intel support; the
[support matrix](../../../../docs/doe-support-matrix.md) keeps this tuple at
"diagnostic host evidence".

## Method

- Harness and tool: unchanged from amd-host round 4. The
  [`dropin_phase_harness.c`](../../../../bench/drop-in/dropin_phase_harness.c)
  sha256 is `4fa1d52a…` and the
  [`dropin_phase_cohort.py`](../../../../bench/tools/dropin_phase_cohort.py)
  sha256 is `4cd33444…`. Workloads, WGSL, inputs and oracles are the same. The
  inputs manifest is byte-identical to the amd-host one (`afe4e1d3…`), and the
  CPU oracle checks every iteration.
- Host: i3-1115G4 (2 cores, 4 threads), 32 GiB, Ubuntu 24.04.3, Linux 6.8,
  Mesa 24.2.8 ANV (Vulkan 1.3.289). The device has one 15.5 GiB `DEVICE_LOCAL`
  heap. Its memory types are `0x1`; `0x7` (device-local, host-visible,
  coherent, write-combined); `0xf` (adds `HOST_CACHED`); and protected `0x21`.
- Providers: Doe C main at `703ba96bf`. Dawn is
  `d04f88359391a12375c1a3e948a0b2dcb4c97a18`, the amd-host revision, with the
  same CMake monolithic-SHARED Release options
  ([build script](builds/build-dawn.sh)). It deviates from the amd-host build
  in two ways, neither on the compute, buffer or queue path:
  `DAWN_USE_X11=OFF`, because the X11-xcb headers are absent, and
  `DAWN_SUPPORTS_CXX_MODULES=OFF`, because gcc 13 cannot scan C++20 modules.
  `dawn-aa` is a byte-identical copy of the Dawn harness.
- Statistics: balanced pairs over all provider permutations, 32 iterations
  per process (iterations 1-31 are warm), and per-pair ratio medians with a
  bootstrap 95% CI (10000 resamples, seed 20261029).
- Window gate (the [tools](tools/) are adapted from amd-host round 4 for i915):
  - Foreign GPU must be at most 2% of window wall time, from i915 fdinfo
    `drm-engine-*` time of non-harness clients.
  - Foreign CPU must be at most 5% of window CPU capacity (4 CPUs).
  - No `make -C runtime/c` process may run at any sample. Another agent uses
    this host for that check.
  - Gate-invalid windows rerun automatically, up to four attempts.
- Host control, an amendment recorded in every contract before the windows
  that count:
  - A resident desktop app (`/usr/lib/chatgpt/ChatGPT`) used 8.5% of CPU
    capacity and about 1% GPU time at idle. That alone breaks the 5% limit
    ([per-process measurement](tools/idle_cpu.py)).
  - The window script pauses it with SIGSTOP and resumes it with SIGCONT in an
    EXIT trap, the analogue of the amd-host participant stop and restart.
    `paused.txt` and `resumed.txt` in every window record this.
  - Paused, a 60 s idle probe measured foreign GPU 0.0% and foreign CPU 2.2%
    ([gate](diagnostics/idle-probe-paused/attempt1/gate.json)).
  - The four baseline-vector attempts before this control were gate-invalid
    (foreign CPU 7.6-10.8%). They are retained in
    [`timed-invalid-host-load/`](timed-invalid-host-load/) and were never
    used.
- Contracts were frozen and hashed before any timing:
  [baseline](contract-baseline.json), [M7R](contract-m7r.json),
  [M2R](contract-m2r.json), [combined](contract-combined.json) and
  [M6-I](contract-m6.json).
  - Amendment 1 was made before any window ran. The foreign-make guard pattern
    was anchored, because the unanchored one matched the launcher shell.
  - Amendment 2 added the host control above. Thresholds, binaries, sampling
    and acceptance never changed.

## Baseline: Doe C 703ba96bf against Dawn d04f88359 (96 pairs per workload)

All three windows passed the gate on their first attempt: foreign GPU 0.0% and
foreign CPU 3.9%, 3.4% and 3.4%. No process failed and every iteration was
oracle-checked. A/A warm operation (dawn-aa/dawn) was 1.008 [0.994,1.021],
1.003 [0.966,1.019] and 0.996 [0.978,1.015].
[Summary](baseline/summary-vs-dawn.json), [verdict](baseline/verdict.txt).

Cells are Doe / Dawn medians in ms, then the paired ratio [CI95].

| Phase | vector 262k | image invert 1024 | upload/readback 1 MiB |
| --- | --- | --- | --- |
| setup | 19.28 / 18.67, 1.025 [1.013,1.042] | 18.42 / 18.39, 1.001 [0.999,1.009] | 19.41 / 18.55, 1.039 [1.026,1.057] |
| compile | 0.350 [0.347,0.354] | 0.291 [0.287,0.296] | (none) |
| pipeline | 2.14 / 2.04, **1.053 [1.044,1.062]** | 2.15 / 2.03, **1.066 [1.061,1.069]** | (none) |
| resources | 0.388 [0.381,0.396] | 2.27 / 2.14, **1.066 [1.054,1.070]** | 0.542 / 0.049, **10.8 [10.6,11.2]** |
| teardown | 0.456 [0.376,0.565] | 0.676 [0.608,0.714] | 0.613 [0.537,0.725] |
| first operation | 0.889 [0.878,0.898] | 0.642 [0.636,0.649] | 0.516 [0.513,0.519] |
| warm upload | 0.492 [0.427,0.629] | 0.223 [0.177,0.264] | 0.499 [0.463,0.531] |
| warm submit | 0.141 / 0.130, **1.283 [1.075,1.355]** | 0.934 [0.833,1.090] | 0.553 [0.484,0.579] |
| warm completion | 0.732 [0.727,0.737] | 4.37 / 3.41, **1.206 [1.135,1.298]** | 0.596 [0.588,0.602] |
| warm map | 0.745 [0.558,0.865] | 1.290 [0.979,1.521] | 0.671 [0.618,0.734] |
| warm operation | 2.58 / 3.60, 0.738 [0.716,0.751], 95/96 | 5.17 / 5.85, 0.892 [0.807,0.920], 71/96 | 1.71 / 2.90, 0.593 [0.579,0.607], 95/96 |
| warm p95 operation | 0.761 [0.753,0.768] | 0.955 [0.925,0.978] | 0.614 [0.596,0.622] |
| complete | 0.747 [0.735,0.759] | 0.843 [0.821,0.863] | 0.603 [0.590,0.624] |
| process CPU | 0.870 [0.858,0.900] | 0.747 [0.726,0.780] | 0.847 [0.809,0.889] |
| peak RSS | 0.865 | 0.891 | 0.906 |

Frozen scoped rule (the same as amd-host C1): **pass on all three
workloads**. No one-time phase exceeds the 1.10 regression bound.

**Fairness audit.** No ratio reaches the suspicious-speedup threshold (10x);
the one 10x ratio is a loss. Doe's warm advantage comes from a real path
difference, not skipped work.
- Dawn uploads through write-combined staging and a GPU copy into
  non-host-visible storage, two copies per submit.
- Doe writes the host-visible, cached destination directly.
- Both complete the same operation, oracle-checked. The phases and effective
  paths are in the code read below.

## Code read and Vulkan call counts (untimed)

The [counting layer](../../amd-vulkan/20261029-dropin-phase/round3/tools/count_layer.c)
from amd-host round 3 was rebuilt here.
[Counts](diagnostics/vulkan-call-counts.jsonl) are per iteration:
(33-iteration count − 1-iteration count) / 32.

| Buffer class | Doe main | M2R | Dawn (`MemoryTypeSelector`, `GetMemoryKindFor`) |
| --- | --- | --- | --- |
| Storage in/out, uniform | `0xf` (cached) | `0x7` (write-combined) | `0x1` (not host-visible), suballocated 8 MiB blocks |
| writeBuffer source | direct memcpy into the `0xf` destination | direct memcpy into `0x7` | `DynamicUploader` staging in `0x7`; image reallocates and maps its 4 MiB staging every iteration |
| MapRead readback | `0xf`, zero-copy map | `0xf` | `0xf` |

On ANV every type is `DEVICE_LOCAL`. Dawn's selector therefore keeps the first
match, `0x1`. M2R's "prefer `DEVICE_LOCAL`" no longer changes heap as it does
on RADV; it only swaps cached for write-combined.

| Per dispatch submit (vector, image) | Doe main | M7R | Dawn |
| --- | ---: | ---: | ---: |
| `vkCreateDescriptorPool` / `vkDestroyDescriptorPool` | 1 / 1 | 0 / 0 | 0 / 0 |
| descriptor allocate / free / update | 1 / 0 / 1 | 1 / 1 / 1 | 0 / 0 / 0 |
| `vkCmdPipelineBarrier` | 4 | 4 | 6 |
| `vkCmdCopyBuffer` | 1 | 1 | 2 |
| fence reset / wait / status | 1 / 1 / 1 | 1 / 1 / 1 | 0.97 / 1 / 2 |

The same structure appears on RADV. Doe's barriers are full
`ALL_COMMANDS|HOST` memory barriers with `HOST_READ` destinations, between
every command (`doe_vk_barrier` in `runtime/c/backends/vulkan/src/commands.c`).
Dawn records precise buffer barriers.

### Owners of the baseline losses

| Loss | Owner, evidence class |
| --- | --- |
| Vector warm submit 1.28x | **Measured.** Per-submit descriptor pool create and destroy. M7R removes them and halves submit (below). |
| Image warm completion 1.21x | **Measured: GPU frequency management (i915), not Doe's work content.** M2R (placement) and M6-I (host-scoped barriers) show no effect. With the GT frequency pinned, the loss reverses (see [the diagnostic](#image-completion-owner-gpu-frequency-diagnostic)). |
| Upload/readback resources 10.8x (+0.49 ms) | **Measured as an accounting split** (the amd-host M8 diagnosis). Doe zero-fills mapped memory at creation; Dawn pays it later. Resources plus first operation: Doe 0.54 + 3.23 ms against Dawn 0.05 + 6.24 ms. |
| Pipeline 1.05-1.07x (+0.1 ms), image resources 1.07x (+0.13 ms), setup up to 1.04x | **Not attributed.** Each is small; the image resources excess is consistent with Doe zero-filling two 4 MiB buffers. |

## M7R on intel-host (192 pairs): FAIL

Contract [`contract-m7r.json`](contract-m7r.json), patch
[`round3/m7r.patch`](../../amd-vulkan/20261029-dropin-phase/round3/m7r.patch)
unchanged. All windows were valid:
- Vector and image passed on attempt 1.
- Upload/readback passed on attempt 3. Attempts 1-2 were gate-invalid because
  the other agent's `make -C runtime/c/native check` started mid-window; they
  are retained.

A/A passed. See the [summary vs main](m7r/summary-vs-main.json) and the
[verdict](m7r/verdict.txt).

| m7r / main | complete | warm submit | warm completion | warm operation | teardown |
| --- | --- | --- | --- | --- | --- |
| vector | **1.052 [1.034,1.066]** | 0.563 [0.529,0.613] | **1.084 [1.075,1.094]** | 1.050 | 1.30 |
| image | 1.000 [0.970,1.030] | 0.538 [0.516,0.565] | **1.063 [1.019,1.107]** | 1.030 | 0.90 |
| upload/readback | 1.000 [0.992,1.007] | 0.98 | 1.00 | 0.994 | 0.99 |

The dispatch gate fails because vector and image `complete` do not fall below
1.0, and the phase guard fails on vector warm completion. On RADV, removing the
pool calls was a net gain. On ANV the submit saving (about 60 µs) is outweighed
by a completion increase (about 180 µs on vector).

*Inference, not verified:* i915 frequency management responds to the changed
CPU and fence-wait pattern. The mean sampled GT frequency over each window,
including the gaps between processes, was 64-182 MHz against a 1250 MHz
maximum (`gtActFreqMhzMean` in each `gate.json`). This result is intel-host
evidence only; the amd-host M7R-2 contract remains frozen and unrun.

## M2R, combined (c-direct) and M6-I on intel-host (192 pairs): all FAIL

The first M2R attempt was blocked. At 20:05Z a `reploid-native-contributor`
user service started on the host and used about 29% of CPU capacity while
idle, so all four M2R-vector attempts were gate-invalid (foreign CPU 13.9-42.2%;
[`m2r-invalid-host-load/`](m2r-invalid-host-load/), retained, unused). The
coordinator then stopped and disabled the service; contributors stay off while
Doppler and Doe are under development.

The nine windows then ran under the unchanged contracts
([runner](runs/run-remaining.sh), [log](runs/run-remaining-master.txt)):
- Every counted window was gate-valid, A/A passed, and no process failed.
- Some attempts were invalidated by the D3D12 agent's `make -C runtime/c`
  checks and its pre-steps; they are retained.
- The M6-I image window exhausted its four attempts, so it was rerun fresh
  under the unchanged contract ([`m6/image_rgba_invert_1024-rerun/`](m6/image_rgba_invert_1024-rerun/),
  [script](runs/rerun-m6-image.sh)). It became valid on attempt 3.

| candidate / main | complete: vector, image, upload/readback | image warm completion | Verdict |
| --- | --- | --- | --- |
| [M2R](m2r/verdict.txt) | 0.997 [0.985,1.001], 0.989 [0.961,1.031], 1.000 [0.992,1.007] | 0.977 [0.929,1.016] | **FAIL** (primary gate) |
| [c-direct](combined/verdict.txt) | **1.048 [1.035,1.059]**, 0.990 [0.965,1.023], 0.998 [0.986,1.007] | 1.043 [1.004,1.102] | **FAIL** (primary gate and phase allowances) |
| [M6-I](m6/verdict.txt) | 1.003 [0.995,1.012], 0.991 [0.963,1.019], 0.997 [0.991,1.000] | 0.985 [0.938,1.015] | **FAIL** (target) |

- **M2R has no measurable effect on ANV.** Write-combined (`0x7`) against
  cached (`0xf`) placement does not change completion. Its separate
  upload-latency gate also fails, on CI width only (upper bounds up to 1.036,
  medians 0.991-1.002).
- **c-direct reproduces the M7R signature.** Submit drops to 0.58 and 0.56,
  but vector warm completion rises to 1.087 [1.077,1.098], violating the phase
  allowance.
- **M6-I passes its other-workload bound and phase guard but not its image
  target.** It changes the shared Vulkan recording path used on RADV, so any
  use needs its own RADV rerun. None is proposed, because it showed no effect
  here.

M6-I ([contract](contract-m6.json), [patch](candidates/m6-barriers.patch)) is
the `commands.c` hunk of the amd-host
[M6 patch](../../amd-vulkan/20261029-dropin-phase/m6/candidate.patch):
barriers between commands use device-only scopes, and only each recording's
final barrier adds the HOST stage and `HOST_READ`.

These are intel-host results only; the amd-host M7R-2, M2R-2 and combined
contracts stay frozen and unrun.

## Image completion owner: GPU frequency diagnostic

This is a diagnostic, not a contract, and claims nothing. The window runs
24 balanced pairs of `image_rgba_invert_1024` (Doe main, Dawn, dawn-aa)
([script](runs/diag-freq.sh)):
- One window with the GT frequency untouched.
- One with `gt_min_freq_mhz` = `gt_max_freq_mhz` = RP0 (1250 MHz), set
  through sudo.
- The original values (min 100, max 1250) are restored by a trap
  ([`freq.txt`](diagnostics/gt-frequency/freq.txt)).

Both windows were gate-valid, and A/A was 0.997 and 0.998.

| image, Doe/Dawn | default governor | GT pinned at 1250 MHz |
| --- | --- | --- |
| warm completion ms | 5.01 / 3.29, **1.42 [1.26,1.54]** | 0.92 / 1.16, **0.79 [0.78,0.80]** |
| warm operation | 1.02 [0.86,1.10] | 0.53 [0.53,0.54] |
| complete | 0.92 [0.85,0.96] | 0.56 [0.56,0.57] |

A 1 ms sampled trace of `gt_act_freq_mhz` covered six runs per provider in
one gate-valid window ([`trace/`](diagnostics/gt-frequency/trace/),
[sampler](runs/diag-freq-trace.py)). Under the default governor the GPU never
exceeded 400 MHz. The median of non-idle samples was 150 MHz in every Doe run
and 200-300 MHz in Dawn runs.

**Owner, measured:** the image completion loss belongs to the i915 frequency
governor's response to each provider's workload. At equal clocks Doe's
GPU-side work completes faster.

*Inference, not verified in the driver:* Dawn's extra per-submit GPU work (a
staging copy) keeps the GPU busier, so the governor clocks it higher. The
same mechanism plausibly explains the M7R and c-direct completion regressions
on this adapter: a faster CPU submit leaves the GPU idle more.

No Doe change follows. Vulkan exposes no frequency control, and adding GPU
work or wait patterns to influence the governor would be a hidden heuristic.

## Correctness on intel-host (untimed, all variants)

Per-variant results are in [`checks/`](checks/).

**Physical clients:** 78 ran for each variant. Main fails five on ANV:
- `format-features`: a device request that should fail succeeds.
- `atomics`: wrong output words.
- `failures`: a pipeline expected to be rejected is created; it fails on RADV
  too.
- `uniform-standard`: wrong output word.
- `limits`: the adapter reports `maxVertexAttributes` below 32 or
  `maxColorAttachmentBytesPerSample` below 128.

M7R, M2R, c-direct and M6 fail exactly the same five.

**Software checks (lavapipe, Mesa 24.2):**
- The aggregate `check` aborts early on this host at `vulkan-creation-errors`.
  The 119 recipe commands were therefore also run one by one.
- Main fails `vulkan-creation-errors`, `msaa-sampling`, `atomics`, `failures`,
  `uniform-standard` and `anisotropy`. The shader-cache commands also fail,
  because the splitter breaks their shell continuations.
- c-direct and M6 match main exactly.
- M7R had one extra failure (`metal-bridge`) and M2R one (`async-submit`).
  Neither reproduced in 10 reruns on any variant, including main.

**`check-vulkan` (lavapipe):** passes for every variant.

### Fixes (test-only; runtime and compiler unchanged)

None of the five ANV failures was a runtime or compiler defect. Each client
asserted something false on this adapter or stale against current native code.

| Client | Root cause | Commit |
| --- | --- | --- |
| `failures` (also failed on RADV) | Injection wrapped `doe_vk_pipeline_create_with_immediates` and `doe_vk_render_pipeline_create`. Native now calls the blob entry points (`runtime/c/native/src/pipelines.c:213`, `render_pipeline.c:131`), so the injected failure never fired (`tests/failures.c:183`). | `cb45d7ec8` |
| aggregate software `check` abort at `vulkan-creation-errors` | Same stale render wrap (`tests/vulkan_creation_errors.c:18`). The aggregate now runs on (56 PASS) to the next, separate lavapipe failure, `msaa-sampling`. | `cb45d7ec8` |
| `atomics`, `uniform-standard` (all Vulkan adapters) | `7d74fd1c6` changed only HLSL to clamp but made the shared clients assert clamping on every backend. SPIR-V still guards (`runtime/c/compiler/src/spirv.c` `access_condition`). Each client now asserts its target's contract; D3D12 values are unchanged. | `7f272c71b` |
| `limits` | It required adapter `maxVertexAttributes` >= 32 (`tests/limits.c:750`). ANV reports 29; WebGPU guarantees 16. The raised check now uses the adapter's value. | `cf60a35bd` |
| `format-features` | It assumed ASTC is unoffered (`tests/format_features.c:214`); ANV offers ASTC LDR. It now requests the first standard feature the adapter does not offer. | `d972a4783` |

**After the fixes, on intel-host:**
- 78 of 78 physical clients pass.
- `check-vulkan` and `make -C runtime/c check` pass.
- On lavapipe the aggregate `check` stops at `msaa-sampling`.

RADV was not rerun, because its GPU is reserved for other work.

## SPIR-V buffer clamp (`1dfd5e009`), 2026-10-30: no detectable difference

Contract [`contract-spirv-clamp.json`](contract-spirv-clamp.json) (frozen
2026-10-31T02:35:53Z) compares the SPIR-V storage/uniform index clamp
`1dfd5e009` (`cand`) against its parent `fac2bc042` (`main`) and source-built
Dawn `d04f88359` (`dawn`, rebuilt because the earlier binaries were deleted),
with a byte-identical Dawn harness as A/A. 48 pairs of 32 iterations per
workload; the two workloads that compile a shader. Window tools are the
[`tools/`](tools/) copies above (the contract's hashes match them); the run
command is [`spirv-clamp/run-window.sh`](spirv-clamp/run-window.sh).

Both windows were valid on attempt 1, with no failed process: foreign GPU 0%,
foreign CPU 1.5% (vector) and 2.4% (image) of capacity against a 5% limit.
A/A warm operation was 0.991 [0.964,1.022] (vector) and 1.005 [0.991,1.056]
(image). Per-metric classifications are in
[`spirv-clamp/verdict.txt`](spirv-clamp/verdict.txt); the summaries are
[vs main](spirv-clamp/summary-vs-main.json) and
[vs Dawn](spirv-clamp/summary-vs-dawn.json).

| cand / main | pipelineNs | warm completion | compile | setup | warm operation | complete | cpuMs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 1.002 [0.999,1.010] | 1.004 [0.994,1.010] | 1.005 | 1.003 | 1.013 | 1.007 [0.996,1.028] | 1.073 [0.984,1.135] |
| image | 0.998 [0.986,1.004] | 1.003 [0.936,1.162] | 1.012 | 0.999 | 1.014 | 0.982 [0.933,1.061] | 0.960 [0.936,1.015] |

Every cand/main confidence interval contains 1.0: under the contract's rule
there is no detectable difference in either primary metric or any reported
metric. Against Dawn, cand and main match each other: pipeline creation is
slower (cand/dawn 1.069 vector, 1.078 image) and compile is faster (0.354,
0.300). Image warm completion against Dawn stays above 1 (1.370), as in the
frequency diagnostic above.

Evidence limits: the contract's claim boundary applies (these two drop-in
workloads, intel-host ANV, these frozen binaries; no Intel or RADV support
claim and no transfer to Metal or D3D12). The raw samples had the absolute work
directory replaced by `<work>/`; the `rawFiles` hashes inside the summaries are
of the unsanitized bytes.

## Remaining owners on intel-host

1. **Image warm completion (1.21x against Dawn):** owned by i915 frequency
   management (measured). There is no Doe mechanism.
2. **Vector warm submit (1.28x):** the owner is per-submit descriptor pool
   create and destroy. The existing mechanism (M7R, also in c-direct) fails on
   ANV, because completion rises in the same way (inference: frequency).
3. **Upload/readback resources:** an accounting split, not a net loss.
4. **Small one-time costs:** pipeline (+0.1 ms), image resources (+0.13 ms)
   and setup (up to +0.9 ms). Not attributed.
5. **Lavapipe software check:** it now stops at `msaa-sampling`;
   `anisotropy` also fails individually. Not investigated here.

No runtime change is committed. Losses, invalid windows and the failed
candidates stay retained.
