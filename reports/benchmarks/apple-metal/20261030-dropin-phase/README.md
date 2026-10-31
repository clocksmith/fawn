# Apple Metal drop-in phase attribution (checkpoint)

Consumer: proof 2 (current Metal confirmation), started ahead of the frozen
Vulkan checkpoint on the owner's instruction. This is a **scoped native drop-in
diagnostic on apple-host**. It is not an application win and does not qualify
Metal support. RADV and ANV evidence does not transfer here, and none of this
transfers back.

**Status: no valid timed window yet.** The B1 windows were invalid. Two B2
vector windows were invalid because another agent's desktop session (screen
capture and computer use) raised the idle GPU utilization from 13-14% to 22-28%.
Timing then stopped. The M1 and M2 candidates passed their correctness gates and
their contracts are frozen and hashed, but **neither has been timed**. The
tables below come from invalid windows. They show the shape of the losses and
are not verdicts.

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
| [`contract-b3.json`](contract-b3.json) | B2 rules on rebuilt binaries | frozen, not yet run | none |
| [`contract-m1r.json`](contract-m1r.json) | M1 rules on rebuilt binaries | frozen, not yet run | none |
| [`contract-m2r.json`](contract-m2r.json) | M2 rules on rebuilt binaries | frozen, not yet run | none |
| [`contract-msl-clamp.json`](contract-msl-clamp.json) | MSL buffer clamp vs main and Dawn | image valid; vector x2, both invalid | image: pipelineNs regression (1.011); vector none |

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

## Remaining owners and open items

1. **Warm map** (M1, frozen, not timed).
2. **Adapter setup** (M2, frozen, not timed). After M2, about 3 ms of adapter
   request remains against Dawn's 7.75 ms. Dawn pays about 5.9 ms more before
   `main`.
3. **Warm submit, about +4 us.** The split is above. Part of it is the
   device-mutex hand-off to the owner-authorized pending worker. Changing when
   the worker wakes would change that design, so it is left for the owner.
4. **Warm pipeline, +0.3 ms one time.** Not split.
5. **Resources on upload/readback**: an accounting split, not a net loss.

## To resume

Restore a quiet desktop: idle GPU at the 13-14% measured at freeze, with no
screen capture or computer-use session. Then rebuild exactly the hashed
binaries and run [`runs/run-b2-m1.sh`](runs/run-b2-m1.sh) and
[`runs/run-m2.sh`](runs/run-m2.sh), whose paths are relative to a work root.

B2 vector has used its one rerun, so its baseline verdict needs either a new
frozen baseline contract or an owner decision. B2 image and upload/readback,
M1 and M2 have not used any attempt. Admitting a busier desktop needs a new
contract and an owner decision; it is not an amendment.
