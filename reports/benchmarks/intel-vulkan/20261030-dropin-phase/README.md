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
| Image warm completion 1.21x | **Unresolved; two hypotheses.** (a) Placement: Doe's GPU reads and writes 8 MiB of cached `0xf` pages, Dawn's uses `0x1`. M2R tests this. (b) Doe's 4 host-scoped barriers per submit. M6-I tests this. Both experiments are frozen and **not yet run** (see blocker). |
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

## M2R, combined (c-direct) and M6-I: frozen, not run (blocker)

At 20:05Z, after the baseline and M7R windows, a `reploid-native-contributor`
user service started on the host. It is enabled, joined a public swarm, and
has zero peers. It uses about 29% of CPU capacity continuously, about 1.2
cores while idle.

All four M2R-vector attempts were therefore gate-invalid: foreign CPU 13.9%,
39.8%, 42.2% and 30.6%, with foreign GPU 3.2% on one attempt
([`m2r-not-run/`](m2r-not-run/), retained, unused). This agent did not stop or
pause a public contributor service without the owner's authorization, so
timing stopped.

[`runs/run-remaining.sh`](runs/run-remaining.sh) runs the nine pending windows
under the unchanged contracts once the host passes the gate.

M6-I ([contract](contract-m6.json),
[patch](candidates/m6-barriers.patch)) is the `commands.c` hunk of the
amd-host [M6 patch](../../amd-vulkan/20261029-dropin-phase/m6/candidate.patch):
- Barriers between commands use device-only scopes.
- Only each recording's final barrier adds the HOST stage and `HOST_READ`.
- **It changes the shared Vulkan recording path used on RADV.** Adopting it
  needs its own RADV rerun.

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

The five ANV physical failures on main are Doe correctness owners on this
adapter. They predate this work and are not addressed here.

## Remaining owners on intel-host

1. Image warm completion (1.21x against Dawn): placement or barriers.
   Frozen as M2R, combined and M6-I, blocked on host load.
2. Vector warm submit (1.28x): owner found (descriptor pool create and
   destroy). The existing M7R mechanism fails on ANV because completion
   regresses.
3. Upload/readback resources: an accounting split, not a net loss.
4. Small one-time costs: pipeline (+0.1 ms), image resources (+0.13 ms) and
   setup (up to +0.9 ms). Not attributed.
5. The five ANV physical correctness failures on main.

No runtime change is committed. Losses, invalid windows and the failed M7R
result stay retained.
