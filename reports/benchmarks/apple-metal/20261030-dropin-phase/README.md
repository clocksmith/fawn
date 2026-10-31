# Apple Metal drop-in phase attribution (checkpoint)

Consumer: proof 2 (current Metal confirmation), started ahead of the frozen
Vulkan checkpoint on the owner's instruction. This is a **scoped native drop-in
diagnostic on apple-host**. It is not an application win and does not qualify
Metal support. RADV and ANV evidence does not transfer here, and none of this
transfers back.

**Status (2026-10-31, later): the Metal completed-handler decision passed its
contract and landed; the identity-worker decision failed on compile time; the M1
upload regression was traced without a Doe-side fix** ([section](#owner-decisions-of-2026-10-31-completion-handler-identity-worker-revised-m1)).

**Earlier status (2026-10-31): first valid baseline; M2 passes; M1 fails its rule.**
The b3, m1r and m2r contracts were frozen on reproducible rebuilds and run
under the shared GPU lock; every verdict uses only valid windows (see
[the results](#valid-results-b3-m1r-m2r-2026-10-31)). The b3 baseline
against Dawn meets the scoped warm-operation rule on image only, and records
one-time regressions in setup (all three) and pipeline (vector and image). M2
passes its mechanism rule. M1 improves warm map and warm operation on every
workload, but fails one no-regression bound: image warm upload is 1.069
[1.063,1.073]. B1 and B2 below are unchanged. Their tables come from invalid
windows and are not verdicts.

## Method

- Host: apple-host, Apple M3 (8 CPU cores, 10 GPU cores), 24 GiB, macOS 26.6.2
  (25G83). The Mac ran on internal battery at 100%, with low-power mode off.
  The desktop stayed live with an external 120 Hz display, a browser and other
  app renderers. The Reploid contributors were disabled. Every process asserted
  a non-CPU Metal adapter.
- Harness: [`bench/drop-in/dropin_phase_harness.c`](../../../../bench/drop-in/dropin_phase_harness.c),
  the amd-host source, ported. The backend is fixed at compile time
  (`-DDOE_PHASE_BACKEND_METAL`), and the adapter must report it. The Apple
  clock is `CLOCK_UPTIME_RAW` because Darwin quantizes `CLOCK_MONOTONIC` to 1 us.
  Darwin `ru_maxrss` is reported in bytes and converted to KiB. Vulkan builds
  keep the original behavior.
- Driver: [`bench/tools/dropin_phase_cohort.py`](../../../../bench/tools/dropin_phase_cohort.py)
  `build --backend metal`. Workloads, generator, seeds and CPU oracles are
  unchanged. The inputs manifest is byte-identical to the amd-host and
  intel-host manifest (`afe4e1d3...`). The oracle checks every iteration
  outside the timed interval.
- Providers:
  - Doe C `libwebgpu_doe_c.dylib`, built from a `git archive` of `d57ab2877`.
  - Dawn `d04f88359391a12375c1a3e948a0b2dcb4c97a18`, the amd-host and
    intel-host revision. Built with CMake Release as a monolithic shared library,
    Metal only.
  - `dawn-aa`, a byte-identical Dawn harness copy used as the A/A control.

  Build receipts are in [`builds/`](builds/).
- Sampling: 96 balanced pairs per workload, 32 iterations per process
  (iteration 0 is reported as `first`), bootstrap 95% CI with 10000 resamples
  and seed 20261029.
- Window gate: [`tools/mac_window.py`](tools/mac_window.py). It checks that the
  power source and low-power mode are unchanged and that there is no thermal
  warning. It runs a 5 s idle-GPU probe (IOAccelerator "Device Utilization %")
  before and after the window, each of which must be at most 25%, and requires
  foreign CPU of at most 20% of run-interval capacity. macOS gives no
  per-process GPU time without root, so foreign GPU work during a window is
  bounded only by the probes on either side. [`tools/wait_quiet.sh`](tools/wait_quiet.sh)
  only schedules windows into quiet slots. Verdicts come from
  [`tools/evaluate.py`](tools/evaluate.py).

## Contracts and windows

| Contract | Purpose | Windows | Verdict |
| --- | --- | --- | --- |
| [`contract-baseline.json`](contract-baseline.json) (B1) | Doe vs Dawn baseline | 3, all invalid | none |
| [`contract-b2.json`](contract-b2.json) | B1 rules; gate defect fixed, CPU limit amended before timing | vector x2, both invalid; image and upload/readback not run | none |
| [`contract-m1.json`](contract-m1.json) | M1 vs main | not run | none |
| [`contract-m2.json`](contract-m2.json) | M2 vs main | not run | none |
| [`contract-b3.json`](contract-b3.json) | B2 rules on rebuilt binaries | 3 valid (vector on its rerun) | image scoped warm-operation win; vector and upload/readback no win; setup and pipeline one-time regressions |
| [`contract-m1r.json`](contract-m1r.json) | M1 rules on rebuilt binaries | 3 valid | **fail** (image warm.upload 1.069) |
| [`contract-m2r.json`](contract-m2r.json) | M2 rules on rebuilt binaries | 3 valid (image on its rerun) | **pass** |
| [`contract-msl-clamp.json`](contract-msl-clamp.json) | MSL buffer clamp vs main and Dawn | image valid; vector x2, both invalid | image: pipelineNs regression (1.011); vector none |
| [`contract-completion.json`](contract-completion.json) | Metal completed handler replaces the worker wake (owner decision 1) | 3 valid (upload/readback on its rerun) | **pass**; landed |
| [`contract-identity.json`](contract-identity.json) | Identity worker at Metal device creation (owner decision 2) | 3 valid | **fail** (compileNs 1.077, 1.087) |

**Why b3, m1r and m2r exist.** The scratch tree holding the frozen binaries
was deleted. A rebuild of the same sources reproduced the harness binaries, the
Dawn library, `libdoe_compiler.dylib` and the inputs manifest byte for byte.
It did not reproduce `libwebgpu_doe_c.dylib` for main, M1 or M2. The native
Makefile builds with `-O2 -g`, and ld64 records each object's modification time
in the debug map. The rebuild therefore sets `ZERO_AR_DATE=1`; with it, two
clean builds of `d57ab2877` give identical bytes ([receipt](builds/b3-rebuild.json)).
Because B2, M1 and M2 name library hashes that can no longer be produced, the
same rules were frozen again as new prospective contracts. B2, M1 and M2 keep
the status recorded above. Every b3, m1r and m2r window runs under the host-wide
`flock /tmp/doe-gpu-timing.lock` ([run script](runs/run-b3-m1r-m2r.sh)). The
Mac was on battery at freeze, and each window's gate records the battery level.

- **B1 was invalid.** Spotlight was indexing after the Dawn build and took
  54-63% of window CPU capacity. The gate also had an accounting defect: it read
  the "after" process table after the 5 s idle probe, so the foreign CPU it
  measured covered a longer interval than the capacity it divided by. The B1
  tool hash is recorded in its contract; the fix is described in
  `contract-b2.json` (`supersedes`).
- **B2 amendments, made before any B2 timing.**
  - The 5% foreign-CPU limit had been copied from the Linux hosts without
    measuring this desktop. The idle desktop measured about 13%, so the limit
    became 20% (`gateAmendment`).
  - The evaluator learned to read the A/A ratio from a separate summary
    (`evaluatorAmendment`).
- **B2 vector was invalid twice.** Foreign CPU was 22-23% and idle GPU
  25-26%: a desktop agent's renderer, `replayd` screen capture and a
  computer-use service were active. Under the contract, one fresh rerun is
  allowed. Both attempts are retained in [`b2/`](b2/).

## Diagnostic shape (B1, invalid windows; not a verdict)

Doe / Dawn medians, then the ratio [CI95] with 96 pairs. Times are in ms. A/A
warm operation was 0.997, 1.000 and 1.007, and every CI contained 1.

| Phase | vector 262k | image invert 1024 | upload/readback 1 MiB |
| --- | --- | --- | --- |
| setup (in `main`) | 19.12 / 8.53, 2.23 [2.03,2.36] | 18.55 / 8.77, 2.08 [2.01,2.16] | 18.93 / 8.56, 2.19 [2.06,2.28] |
| pipeline | 1.61 / 1.30, 1.25 [1.24,1.26] | 1.67 / 1.36, 1.23 [1.21,1.25] | (none) |
| resources | 0.225 / 0.435, 0.52 | 0.741 / 0.461, 1.60 | 0.144 / 0.016, 9.07 |
| first operation | 2.02 / 1.92, 1.09 | 2.98 / 3.13, 0.94 | 2.56 / 2.78, 0.92 |
| warm upload | 0.97 | **0.30** (Doe 0.104, Dawn 0.346) | 0.99 |
| warm submit | **1.38 [1.35,1.40]** | 0.96 | **2.08 [2.03,2.12]** |
| warm completion | 0.93 | **0.64** | 0.91 |
| warm map | **2.28 [2.26,2.31]** | **2.29 [2.23,2.32]** | **2.30 [2.28,2.32]** |
| warm operation | 0.344 / 0.350, 1.00 [0.98,1.02] | 0.786 / 1.185, 0.66 [0.66,0.67] | 0.311 / 0.310, 1.01 [1.00,1.01] |
| process wall | 1.07 [1.06,1.09] | 0.91 | 1.12 [1.11,1.15] |
| process CPU | 0.91 | 0.81 | 0.87 |
| peak RSS | 0.86 | 0.94 | 0.89 |

The invalid B2 vector attempts reproduce this shape: map 2.33-2.37, submit
1.33-1.34, completion 0.94, setup 2.23-2.24, process wall 1.10-1.11, A/A about
1.00.

## Side-by-side owner read (Doe `d57ab2877` against Dawn `d04f88359`)

| Mechanism | Doe Metal | Dawn Metal | Effect here |
| --- | --- | --- | --- |
| Storage modes | Every public buffer is `MTLResourceStorageModeShared` with default options (`runtime/c/native/src/backend_buffers.c:15-28`, `runtime/c/backends/metal/src/objc_runtime.c:149-163`) and is zeroed by memset at creation (`backends/metal/src/resources.c:28-35`) | Mappable buffers are Shared; every other buffer is Private (`src/dawn/native/metal/BufferMTL.mm:69-75`), zeroed lazily by GPU `fillBuffer` (`BufferMTL.mm:247-307`) | Doe uploads with a direct CPU write (`native/src/buffers.c:431-442`, `resources.c:79-97`). Dawn stages Private destinations through its uploader, which is why Doe is 0.30x on the 4 MiB upload. Creation-time zeroing moves cost into `resources` (9x on upload/readback). Dawn pays its equivalent lazily, so `first operation` is 0.92 and the net is neutral. |
| Command buffer and encoders per submit | One `commandBufferWithUnretainedReferences` per submit, created at submit (`backends/metal/src/objc_commands.c:4-17`, `native/src/metal_progress.c:58-72`). One encoder per copy, fill or dispatch, each inside its own autorelease pool (`objc_commands.c:19-103`). All 31 buffer slots are bound and sizes go through `setBytes` (`objc_commands.c:56-77`) | One retained `[queue commandBuffer]`, prepared at the end of the previous submit (`CommandRecordingContext.mm:63-76`, `QueueMTL.mm:253`). Scheduled and completed handlers, `encodeSignalEvent` and commit (`QueueMTL.mm:203-254`) | The [submit probe](diagnostics/submit_probe.txt) runs both native call sequences on the same device and finds them equal (Doe shape 4.7 us, Dawn shape 4.4-4.8 us). Retained vs unretained buffers, per-call pools and Shared vs Private source make no difference. The [submit split](diagnostics/submit_split_diag.txt) puts Doe's extra ~4 us in its own wrapping: record bookkeeping around the native calls, about 0.5 us around commit, and a device-mutex hand-off of about 1 us. That hand-off happens because `link_pending` wakes the pending worker under the mutex (`native/src/queue.c:295-301`, `native/src/pending_progress.c:170-175`), and the worker then contends for it (`pending_progress.c:33-49`). |
| Completion wait | `wgpuQueueOnSubmittedWorkDone` settles synchronously before admitting a completed future (`native/src/queue.c:600-631`): `settle_pending` (`queue.c:155-210`) calls `native_command_wait` (`native/src/backend_commands.c:40-54`), which runs `waitUntilCompleted` under the device mutex (`objc_commands.c:132-151`). Each commit encodes a shared-event signal (`backends/metal/src/commands.c:224-243`). The pending worker waits on `waitUntilSignaledValue:timeoutMS:` outside the mutex (`objc_commands.c:157-179`, `pending_progress.c:91-105`); see `ec6574c1f` and `1e9741b6a` | `addCompletedHandler` advances the completed serial (`QueueMTL.mm:234-240`). `WaitAny` blocks on a condition variable over that serial (`src/dawn/native/ExecutionQueue.cpp:76-86,123-133`) | Doe completes faster: 0.93, 0.64 and 0.91. Not an owner of loss. |
| Pipeline and MSL | `newLibraryWithSource:options:nil` behind a 16-entry in-process source cache (`objc_pipeline.c:6-22`, `objc_library_cache.c`), then `newComputePipelineStateWithFunction:` (`objc_pipeline.c:59-96`). The binary archive is opt-in (`objc_archive.c:65-76`). The generated MSL guards every storage access and reads sizes from `buffer(30)` (`sizes[1]`, `sizes[2]`) | Tint MSL, `newLibraryWithSource` with explicit options (`ShaderModuleMTL.mm:548-590`). Storage-buffer lengths are also passed with `setBytes` (`CommandBufferMTL.mm:544-560,659`) | Both pay about 42-69 ms on a cold Metal compiler cache and 1.3-1.7 ms warm. Doe is 1.23-1.25x warm (+0.3 ms, one time). The slot-30 mechanism is matched, so it is not the owner. Not split further. |
| Map and readback | Read mappings `malloc` a shadow and copy the whole range (`native/src/buffers.c:322-329`). Metal is not lendable (`backend_buffers.c:53-58`), unlike Vulkan (`7379fd2fe`) and D3D12 READBACK (`38163dd1f`) | Returns the `contents` pointer; unmap is a no-op (`BufferMTL.mm:220-229`) | **Owner: warm map 2.28-2.30x on all three workloads.** That is +22 us on the 1 MiB workloads and +112 us on the 4 MiB one. |
| Setup | Every adapter and device context calls `MTLCreateSystemDefaultDevice` (`objc_runtime.c:106-133`, `native/src/backend.c:67-79,161-174`). Frameworks are loaded by `dlopen` inside `setup` (`objc_runtime.c:263-266`) | macOS enumerates with `MTLCopyAllDevices` (`BackendMTL.mm:89`; `MTLCreateSystemDefaultDevice` is used only on iOS, `:99-102`). Metal, Foundation, Cocoa, QuartzCore and IOSurface are linked at load time | `setup` is not like-for-like, because Dawn pays about 5.9 ms more before `main` (`processWall - processInMainNs`). Process wall is the fair one-time measure: +3.8 ms (vector) and +4.9 ms (upload/readback). The [setup probe](diagnostics/setup_probe.txt) measures `MTLCreateSystemDefaultDevice` at 14-23 ms in a fresh process against 7.2-8.0 ms for `MTLCopyAllDevices`. On this one-GPU host both return the same object ([identity probe](diagnostics/identity_probe.txt)). **Owner: about 7 ms of adapter setup.** |

## Candidates (correctness passed, contracts frozen, not timed)

| Candidate | Mechanism | Pre-freeze correctness |
| --- | --- | --- |
| [M1](m1/candidate.patch) | Lend the Shared allocation to read mappings under exactly the admission of a read: not poisoned, initialized, no recorded or pending owner after the existing settle. Adds `doe_mtl_buffer_host_range`; `doe_mtl_buffer_read` now uses it; `native_buffer_host_lendable` admits Metal. Write mappings and mapped-at-creation keep their copies. | 81 Metal physical clients match main per client ([main](correctness/physical-main.tsv), [M1](correctness/physical-m1.tsv)). `backends/metal` `check`, `native-check` and `native-graphics-check` pass. |
| [M2](m2/candidate.patch) | Obtain the system default device through `MTLCopyAllDevices` when it reports exactly one device; any other count keeps `MTLCreateSystemDefaultDevice`. A missing symbol keeps the old call. The adapter and device contexts compare the same native object as before (`backend.c:164`). | 81 clients match main ([M2](correctness/physical-m2.tsv)). `check`, `sdk-abi`, `native-check` and `native-graphics-check` pass. In untimed smoke runs the adapter request went from 16.7-18.3 ms to 10.6-11.2 ms. |

Physical-client totals, identical on main, M1 and M2:
- 58 pass.
- 16 abort, all on main as well, mostly Vulkan-, D3D12- or format-specific
  clients run under Metal (see the TSV files).
- `metal-async-physical` exits 2 without its checker arguments.
- 6 are GNU-only and cannot be built on Darwin.

The Makefile's Darwin `PHYSICAL_CLIENTS` list holds only the 8 late appends, so
the full list was run with [`runs/physical.sh`](runs/physical.sh).

## Valid results (b3, m1r, m2r), 2026-10-31

Ten windows ran between 02:15Z and 05:06Z, each under the shared GPU lock. The
Mac was on battery at freeze (34%), was plugged in before the first window, and
stayed on AC with the battery at 52-80% for every window. No window changed
power source. Idle GPU was 0% (b3) and 10-11% (m1r, m2r) before and after each
window, and foreign CPU in the valid windows was 6.3-12.9%. Gates, raw samples,
run logs, summaries and evaluator output are in [`b3/`](b3/), [`m1r/`](m1r/)
and [`m2r/`](m2r/).

- **Invalid windows (retained, not used).** b3 vector attempt 1 had foreign CPU
  of 25.0%, all of it another session's one-core Python job. m2r image attempt 1
  passed its gate but failed the A/A rule: 0.982 [0.964,0.998] does not contain
  1. Each workload's single permitted rerun was valid.
- **Scheduling change (not a validity rule).** `wait_quiet.sh` reads the
  decaying `ps %cpu`, so it admitted the fresh one-core job that invalidated b3
  vector attempt 1. From then on the run script also waits for 20 consecutive
  1 s samples in which no process exceeds 50% of a core and the total stays
  under 120% ([run script](runs/run-b3-m1r-m2r.sh)). Validity still comes only
  from `tools/mac_window.py` and the A/A rule.

### b3: Doe C `d57ab2877` against Dawn `d04f88359` (baseline)

Medians are in ms, then Doe/Dawn [95% CI], with 96 pairs per workload. A/A
warm operation was 0.998 [0.996,1.003], 1.002 [0.992,1.014] and 1.003
[0.999,1.006].

| Phase | vector 262k | image invert 1024 | upload/readback 1 MiB |
| --- | --- | --- | --- |
| setup | 16.67 / 8.16, 2.055 [2.013,2.087] | 17.03 / 8.45, 2.020 [2.006,2.028] | 16.40 / 7.71, 2.144 [2.124,2.161] |
| compile | 0.119 / 0.288, 0.409 [0.403,0.414] | 0.100 / 0.286, 0.351 [0.347,0.353] | (none) |
| pipeline | 1.761 / 1.433, 1.221 [1.213,1.228] | 1.790 / 1.451, 1.228 [1.216,1.236] | (none) |
| resources | 0.226 / 0.432, 0.520 [0.509,0.524] | 0.752 / 0.450, 1.670 [1.639,1.686] | 0.133 / 0.015, 8.97 [8.77,9.09] |
| first operation | 2.254 / 2.097, 1.079 [1.065,1.090] | 3.048 / 3.333, 0.923 [0.912,0.930] | 2.441 / 2.699, 0.910 [0.895,0.918] |
| warm upload | 0.969 [0.946,1.000] | 0.094 / 0.342, 0.277 [0.275,0.279] | 0.997 [0.966,1.026] |
| warm submit | 0.0123 / 0.0090, 1.361 [1.319,1.390] | 0.992 [0.963,1.021] | 0.0071 / 0.0033, 2.107 [2.076,2.139] |
| warm completion | 0.952 [0.947,0.954] | 0.472 / 0.713, 0.663 [0.660,0.670] | 0.917 [0.915,0.920] |
| warm map | 0.0403 / 0.0171, 2.337 [2.310,2.351] | 0.192 / 0.086, 2.219 [2.172,2.273] | 0.0375 / 0.0166, 2.261 [2.249,2.272] |
| **warm operation** | 0.364 / 0.355, **1.025 [1.022,1.031]** | 0.777 / 1.168, **0.669 [0.662,0.682]** | 0.313 / 0.310, **1.013 [1.006,1.015]** |
| warm p95 operation | 1.028 [1.003,1.051] | 0.682 [0.678,0.690] | 1.146 [1.054,1.237] |
| complete | 1.023 [1.010,1.026] | 0.744 [0.739,0.750] | 1.015 [1.001,1.025] |
| process wall | 47.97 / 44.50, 1.070 [1.064,1.081] | 65.52 / 73.05, 0.895 [0.891,0.903] | 37.00 / 33.74, 1.109 [1.099,1.117] |
| process CPU | 0.905 [0.901,0.915] | 0.804 [0.802,0.808] | 0.854 [0.850,0.860] |
| peak RSS | 0.868 | 0.946 | 0.886 |

Verdict ([evaluator output](b3/eval.txt)):
- Image meets the scoped warm-operation rule: warm and p95 CI upper bounds are
  below 1, CPU is 0.80 and RSS is 0.95.
- Vector (1.025) and upload/readback (1.013) do not.
- One-time regressions are recorded for setup on all three workloads and for
  pipeline on vector and image.

This is a scoped native drop-in phase result on apple-host only. It is not an
application win, does not qualify Metal support and does not transfer to Vulkan.

### m1r: lend Shared memory to read mappings (M1/main)

| M1/main | vector | image | upload/readback |
| --- | --- | --- | --- |
| warm map | 0.415 [0.412,0.420] | 0.432 [0.427,0.435] | 0.447 [0.436,0.459] |
| warm operation | 0.930 [0.925,0.935] | 0.877 [0.869,0.880] | 0.912 [0.900,0.925] |
| warm p95 operation | 0.938 [0.919,0.958] | 0.878 [0.863,0.889] | 0.940 [0.913,0.976] |
| warm upload | 0.953 [0.927,0.970] | **1.069 [1.063,1.073]** | 0.934 [0.917,0.953] |
| complete | 0.951 [0.928,0.974] | 0.896 [0.888,0.903] | 0.936 [0.915,0.965] |
| process CPU | 0.971 | 0.910 | 0.960 |

**Verdict: FAIL** ([evaluator output](m1r/eval.txt)). Every improve metric
passes on every workload. Image warm upload (`wgpuQueueWriteBuffer` of 4 MiB)
breaks the 1.05 no-regression bound: its CI lower bound is 1.063, about +6 us
on 92 us. Every other no-regression metric passes. The verdict stands, and M1
is not landed. The upload regression has not been attributed. A changed or
re-measured M1 needs a new prospective contract. Diagnostic only: M1/Dawn warm
operation was 0.949, 0.641 and 0.927.

### m2r: single-device enumeration (M2/main)

| M2/main | vector | image | upload/readback |
| --- | --- | --- | --- |
| setup | 0.539 [0.535,0.545] | 0.551 [0.546,0.553] | 0.509 [0.500,0.533] |
| process wall | 0.833 [0.816,0.845] | 0.878 [0.872,0.882] | 0.799 [0.787,0.815] |
| warm operation | 0.998 [0.994,1.003] | 0.994 [0.989,1.001] | 0.994 [0.988,1.000] |

**Verdict: PASS** ([evaluator output](m2r/eval.txt)). Setup and process wall
have CI upper bounds below 1 on every workload, and every no-regression metric
stays within 1.05. Setup falls from 16.4-17.7 ms to 8.6-9.4 ms. The claim
boundary applies: this accepts the mechanism for the Metal C runtime on
apple-host only, and is not a scoped win against Dawn. Diagnostic only: M2/Dawn
setup is still 1.14-1.16, while process wall is 0.81-0.91.

## MSL buffer clamp (`591fde1ee`), 2026-10-30: image pipeline regression, vector invalid

Contract [`contract-msl-clamp.json`](contract-msl-clamp.json) (frozen
2026-10-31T02:37:12Z) compares the MSL clamp, direct word access, inline scalar
constants and zero-init removal `591fde1ee` (`cand`, on top of the SPIR-V-only
`1dfd5e009`) against `fac2bc042` (`main`) and Dawn `200d6641a` (`dawn`, the
repository's existing vendor build, not this campaign's `d04f88359`), with a
byte-identical Dawn harness as A/A. 48 pairs of 32 iterations per workload.
Each window ran under `flock /tmp/doe-gpu-timing.lock` after an untimed warm-up
pass ([`msl-clamp/run-metal.sh`](msl-clamp/run-metal.sh)); the window tools are
the [`tools/`](tools/) copies above (the contract's hashes match them).

- **Image: valid on attempt 1.** Foreign CPU was 19.96% against the 20%
  limit, idle GPU 0%, on AC power. No process failed; A/A warm operation was
  0.997 [0.993,1.003]. Per-metric classifications are in
  [`msl-clamp/image_rgba_invert_1024/verdict.txt`](msl-clamp/image_rgba_invert_1024/verdict.txt).
- **Vector: invalid twice; no verdict.** Foreign CPU was 25.8% and 22.3%. The
  top foreign process in both was a Python interpreter (about 9 CPU-seconds),
  then Chrome and WindowServer. The contract allows two attempts, so vector has
  no result. Both gates and raw samples are retained; the attempt 2 summaries
  and [classification](msl-clamp/vector_scale_add_262k/attempt2-diagnostic.txt)
  are diagnostic only.

| image, cand / main | median | CI95 | classification |
| --- | --- | --- | --- |
| pipelineNs (primary) | 1.011 | [1.003,1.021] | regression |
| warm completion (primary) | 0.998 | [0.989,1.004] | no detectable difference |
| compileNs | 0.983 | [0.969,1.000] | no detectable difference |
| setupNs | 1.004 | [0.993,1.013] | no detectable difference |
| warm operation | 0.998 | [0.995,0.999] | improvement |
| complete | 0.997 | [0.990,1.003] | no detectable difference |
| processWall | 1.002 | [0.992,1.009] | no detectable difference |
| cpuMs | 1.003 | [0.998,1.007] | no detectable difference |

Under the contract's rule, image pipeline creation regressed by about 1.1%
(CI lower bound 1.003), and warm operation improved by about 0.2%. The other
metrics show no detectable difference. Against Dawn, cand and main match each
other: pipeline 1.122 and 1.107, setup 2.08, warm completion 0.756.

Evidence limits: the contract's claim boundary applies (these drop-in
workloads, apple-host, these frozen binaries; no Metal support claim and no
transfer to Vulkan or D3D12). The image window passed with foreign CPU 0.04
points under its limit. The raw samples and gate commands had the absolute work
directory replaced by `<work>/`; the `rawFiles` hashes inside the summaries are
of the unsanitized bytes.

## Owner decisions of 2026-10-31: completion handler, identity worker, revised M1

Both contracts compare a candidate (`cand`) against main `1c585a5cc` and Dawn
`d04f88359`, with the b3/m1r gate, 96 pairs of 32 iterations per workload and the
byte-identical Dawn A/A control. They were frozen before timing; build receipts are
in [`builds/completion-identity-build.json`](builds/completion-identity-build.json)
and the run script in [`runs/run-completion-identity.sh`](runs/run-completion-identity.sh).
Seven windows ran between 12:50Z and 13:10Z under the shared GPU lock, on battery
at 84-86% with low-power mode off and no power-source change; idle GPU was 7-9%
before and after, and foreign CPU 6.4-13.7%. Completion upload/readback attempt 1
passed its gate but failed the A/A rule (0.995 [0.992,0.999]); it is retained and
its single rerun was valid. Before freezing, all 81 Metal physical clients gave the
same per-client result on main and both candidates
([main](correctness/physical-main-1c585a5cc.tsv), [completion](correctness/physical-p1-1c585a5cc.tsv),
[identity](correctness/physical-p2-1c585a5cc.tsv); 61 pass, 13 abort on main too,
1 needs checker arguments, 6 GNU-only), and `backends/metal` `check`, `sdk-abi`,
`native-check` and `native-graphics-check` passed on both trees.

### Decision 1: Metal completion through the completed handler (pass, landed)

Mechanism ([patch](completion/candidate.patch)): every committed command registers
`addCompletedHandler:` after its last fallible encode (`backends/metal/src/commands.c:240-252`,
`objc_commands.c:136`), as Dawn does (`QueueMTL.mm:234-240`). A live device's
`link_pending` and lease drops no longer wake the progress worker
(`native/src/queue.c:305`, `lifecycle.c:290`), and the worker does not service
Metal records until the device is destroyed (`pending_progress.c:29-33`). The
handler never takes the device mutex and never retires state; it counts and, once
`wgpuDeviceDestroy` has armed it (`lifecycle.c:527`), wakes the worker through a
refcounted record that outlives the device (`metal_progress.c:40-99`). Live devices
retire records on their next public call, which every submit, write, map and work
done already settles. Charter text recording the 2026-10-24 worker approval now
records this decision as superseding it for Metal (`native/AGENTS.md`,
`native/README.md`, `backends/metal/README.md`, `backends/metal/CATSCAN.md`).

| cand/main | vector | image | upload/readback |
| --- | --- | --- | --- |
| warm submit (improve) | 0.883 [0.865,0.898] | 0.850 [0.829,0.868] | 0.691 [0.682,0.701] |
| warm map | 0.956 [0.954,0.959] | 0.957 [0.947,0.965] | 0.965 [0.961,0.969] |
| warm operation | 0.987 [0.982,0.989] | 0.985 [0.978,0.991] | 0.988 [0.973,0.996] |
| warm completion | 0.999 [0.995,1.001] | 1.002 [0.994,1.008] | 0.999 [0.990,1.005] |

**Verdict: PASS** ([evaluator output](completion/eval.txt)); every no-regression
metric is within 1.05 on every workload. Against Dawn (diagnostic, from
[`summary-vs-dawn.json`](completion/summary-vs-dawn.json)), warm submit moves from
1.311 to 1.162 [1.119,1.194] on vector (+2.8 us to +1.5 us), from 2.147 to 1.479
[1.453,1.493] on upload/readback (+3.5 us to +1.5 us) and from 1.049 to 0.906 on
image. Warm operation against Dawn is 1.018 [1.014,1.021], 0.683 and 0.985
[0.976,0.993]. The claim boundary applies: Metal C runtime on apple-host only.

### Decision 2: identity worker at Metal device creation (fail, not landed)

Mechanism ([patch](identity/candidate.patch)): `lifecycle.c:334` also starts the
worker for Metal. A diagnostic build compared the worker digest with an inline
recomputation in the same process: equal in 4 of 4 processes, same digest across
processes. The worker digest is per device and joined on teardown, as for D3D12.

| cand/main | vector | image | upload/readback |
| --- | --- | --- | --- |
| pipelineNs (improve) | 0.885 [0.878,0.892] | 0.886 [0.878,0.891] | (no pipeline) |
| compileNs | **1.077 [1.061,1.083]** | **1.087 [1.072,1.098]** | (no shader) |
| setupNs | 1.005 [1.001,1.010] | 1.004 [0.998,1.010] | 1.003 [0.997,1.007] |
| cpuMs | 1.003 | 1.000 | 1.025 [1.021,1.033] |

**Verdict: FAIL** ([evaluator output](identity/eval.txt)). Pipeline creation falls
by 0.20 ms (Dawn ratio 1.17 to 1.03-1.06), but shader-module compilation, which
runs while the worker is still hashing, rises by 7-9 us and breaks the 1.05 bound.
Device creation takes about 0.2 ms, shorter than the 0.49 ms hash, so the worker
overlaps compile rather than only device open. The verdict stands and the change
is not landed. A changed start point or a changed rule needs an owner decision and
a new prospective contract.

### Decision 3: revised M1 (traced; no fix found; not frozen)

The [trace](diagnostics/m1_upload_trace.txt) localizes the m1r image warm-upload
regression to the first CPU read of the caller's 4 MiB source array inside
`wgpuQueueWriteBuffer` (+3.6% on that read); Doe's own lock, settle, unlock and copy
are unchanged. It depends on whether the harness memcpy read the lent shared
allocation or a malloc shadow, survives cache flushes, extra Doe-side traffic,
spins and the completion change, and does not reproduce in a
[standalone Metal probe](diagnostics/m1_upload_probe.m). With no Doe-side cause
found there was nothing to fix, so no revised M1 was frozen or timed.

## Remaining owners (after b3, m1r, m2r)

These rows compare Doe `d57ab2877` with Dawn `d04f88359` side by side. Each
mechanism is labelled measured, diagnostic or read.

1. **Adapter setup.** M2 passed and landed in `85ceeb1a4`. On that main, all 81 physical clients matched unpatched `e0220ddcf` client by client ([main](correctness/physical-main-e0220ddcf.tsv), [M2](correctness/physical-m2-on-e0220ddcf.tsv)). It removes about
   7.6-8.4 ms per process. Diagnostic only: M2/Dawn setup is still 1.14-1.16 and
   adapter request 1.24-1.26. Dawn pays more before `main`, and on process wall
   Doe with M2 is ahead (0.81-0.91).
2. **Warm map, 2.22-2.34x Dawn (measured).** Read mappings copy into a
   `malloc` shadow (`native/src/buffers.c:322-329`; Metal is not lendable at
   `backend_buffers.c:53-58`). Dawn returns `contents` (`BufferMTL.mm:205-229`).
   M1 removes 55-59% of warm map but failed its frozen rule on image warm
   upload. That regression is unattributed, so the owner stays open.
3. **Pipeline, +0.33-0.34 ms (1.22-1.23x, measured; split diagnostic).** The
   [pipeline split](diagnostics/pipeline_split_diag.txt) (fresh process, warm
   caches, six runs per workload, [patch](diagnostics/pipeline_split_diag.patch))
   gives Doe's 1.67-1.74 ms as:
   - **0.48-0.50 ms hashing the loaded images** for the persistent translation
     cache key. `stages.c:121` reserves the disk cache, and `stages.c:136`
     calls `native_shader_cache_identity` inline. That function streams SHA-256
     over every originally read-only mapped segment of both Doe images
     (`shader_cache_identity.c:258-330`). The background identity worker that
     overlaps this starts only for D3D12 (`lifecycle.c:334`).
   - 0.04 ms for the disk lookup (`stages.c:144`). On a hit it replaces
     specialization, which costs 0.15 ms on a miss (`stages.c:149`).
   - Metal calls: `newLibraryWithSource` 0.51-0.56 ms with `options:nil`
     (`objc_pipeline.c:15`), `newFunctionWithName` 0.007 ms and
     `newComputePipelineStateWithFunction` 0.58-0.64 ms (`objc_pipeline.c:81`).
   - Dawn spends 1.43-1.45 ms on Tint MSL generation (`ShaderModuleMTL.mm:525`),
     `newLibraryWithSource` with `MTLCompileOptions` (`ShaderModuleMTL.mm:538-578`)
     and `newComputePipelineStateWithDescriptor` (`ComputePipelineMTL.mm:82`).
     This standalone build has no persistent translation cache.

   On these shaders the cache key costs about 0.5 ms per process to save
   0.15 ms. Without the hash, Doe's split sums to about 1.2 ms. That figure is
   an inference from the split, not a measured pipeline phase. Options include
   starting the identity worker for Metal, deferring the key, or skipping the
   persistent cache for small modules. Each changes cache policy, so the
   choice belongs to the owner. No candidate is frozen.
4. **Warm submit, +3.3 us (vector) and +3.8 us (upload/readback); image 0.99
   (measured).** The probe in the side-by-side read above found the native
   call sequences equal: Doe shape 4.5 us, Dawn shape 4.7-4.9 us. Read side by
   side, the remaining difference is in how completion is wired:
   - Doe holds the device mutex for the whole submit. It records the batch into
     a new command buffer (`metal_progress.c:58-72`, `objc_commands.c:4-17`)
     and links the pending record. That signals the progress worker before
     commit (`queue.c:295-301,361-362`, `pending_progress.c:170-175`). After
     commit and a locked poll (`queue.c:378`) it unlocks. The woken worker has
     been blocked on that mutex (`pending_progress.c:33-49`), then settles and
     waits on the shared event outside it (`pending_progress.c:91-103`).
   - Dawn encodes, then adds scheduled and completed handlers
     (`QueueMTL.mm:224,234`), encodes the event signal and commits
     (`QueueMTL.mm:247-250`), then prepares the next command buffer
     (`QueueMTL.mm:253`). Metal's own handler thread delivers completion, and
     no Dawn thread is woken per submit.
   - The earlier split puts about 0.7-2.3 us in the unlock hand-off to the
     woken worker, plus about 0.5-0.7 us of record bookkeeping. Changing the
     wake would change the owner-authorized worker design (2026-10-24), so no
     candidate is frozen.
5. **Resources on upload/readback, 8.97x (measured).** Creation-time memset
   zeroing (`backends/metal/src/resources.c:28-35`) moves cost into resources.
   Dawn clears lazily on the GPU (`BufferMTL.mm:247-307`), so its first
   operation is slower (Doe/Dawn 0.91). This is an accounting split, not a net
   loss.

## To resume

The binaries are reproducible from [the rebuild receipt](builds/b3-rebuild.json).
Use `ZERO_AR_DATE=1`; the run script is
[`runs/run-b3-m1r-m2r.sh`](runs/run-b3-m1r-m2r.sh), with `<repo>` and `<work>`
substituted. B3, M1R and M2R have used their attempts and have verdicts. A
changed M1, any pipeline or submit candidate, or a baseline that includes M2
needs a new prospective contract. B1, B2, M1 and M2 keep the status recorded
above.
