# Windows AMD Vulkan drop-in phase baseline and candidate contracts

Consumer: proof 1 engineering attribution, as **separate tuple evidence**. The
host is windows-amd-host: an AMD Ryzen 7 6800H with integrated Radeon (RDNA2,
vendor `0x1002`, device `0x1681`), the AMD proprietary Windows driver
25.10.30.02 (Vulkan 1.4.315), Windows 10.0.26300. It is not the initial
qualification target (Strix Halo, RADV), and it is not the D3D12 tuple on the
same machine. Nothing here transfers to RADV, ANV, D3D12 or Metal, and none of
their records transfer here. This is a **scoped native drop-in diagnostic**,
not the GOALS Vulkan application win. The [support matrix](../../../../docs/doe-support-matrix.md)
does not list this tuple, so it stays unsupported; this record does not change that.

These are Doe's first Windows Vulkan measurements.

## Build path (task 1)

No new build path was needed. The Windows SDK build
([`build_windows.py`](../../../../runtime/c/native/tools/build_windows.py))
already compiles every canonical owner from `runtime/c/native/sources.mk`,
including `backends/vulkan`, into `webgpu_doe_c.dll` (`build_windows.py:27-54,160-164`),
and links the Vulkan loader into its Win32 loader control (`:186-195`). The
Vulkan owner loads `vulkan-1.dll` from System32 only
(`runtime/c/backends/vulkan/src/loader.c:329-333`). Windows defaults an
undefined backend request to D3D12 (`runtime/c/native/src/lifecycle.c:137-145`),
so the harness requests `WGPUBackendType_Vulkan` explicitly
(`bench/drop-in/dropin_phase_harness.c`, `PHASE_BACKEND`) and asserts it, as on
Linux. Toolchain: the Chromium-bundled clang-cl 24 with the VS 2026 Build Tools
SDK environment, the same route as the D3D12 campaign.

Dawn `d04f88359391a12375c1a3e948a0b2dcb4c97a18` was built standalone with
CMake/Ninja and MSVC 14.51: monolithic SHARED Release, Vulkan and Null only
(D3D11, D3D12, OpenGL, SwiftShader off), the Tint HLSL/MSL/GLSL writers off
([script](tools/dawn_build.py), [options](builds/builds.json)). One option
deviates from a default and is required on this host:

- `DAWN_FORCE_SYSTEM_COMPONENT_LOAD=ON`. Without it, Dawn opens
  `vulkan-1.dll` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`
  (`src/dawn/common/DynamicLib.cpp:93-97`). That flag pair rejects the bare
  name tried last from the runtime search paths (`src/dawn/native/Instance.cpp:333-341`,
  `src/dawn/common/DynamicLib.cpp:142-150`) with Windows error 87, and the
  adapter request fails. With the option, Dawn uses `LOAD_LIBRARY_SEARCH_SYSTEM32`
  (`DynamicLib.cpp:90-91`), the same System32 loader Doe uses. It is a library
  search rule only; no Vulkan, compute, buffer or queue path changes.

The harness and cohort tool gained a Windows route (commit `b558c1456`):
QueryPerformanceCounter phase times, GetProcessTimes CPU, peak working set,
clang-cl import-library linking, and a refusal of any harness not beside its
DLL, so the executable directory decides which provider loads.

## Method

- Harness [`dropin_phase_harness.c`](../../../../bench/drop-in/dropin_phase_harness.c)
  sha256 `bcd22b6a…`, tool [`dropin_phase_cohort.py`](../../../../bench/tools/dropin_phase_cohort.py)
  `7a181fab…`. The three frozen workloads, WGSL, inputs and oracles are
  unchanged; the input manifest is byte-identical to the amd-host and
  intel-host manifests (`afe4e1d3…`). The CPU oracle checks every iteration
  after the operation interval closes. 32 iterations per process; 1-31 are warm.
- Providers: Doe C main `cb7e56ecd`; Dawn `d04f88359`; `dawn-aa`, a
  byte-identical Dawn harness copy. Every process reported the physical
  `AMD Radeon(TM) Graphics` Vulkan adapter (`adapterType` integrated).
- Balanced pairs over all provider permutations, per-pair ratio medians,
  bootstrap 95% CI (10000 resamples, seed 20261029).
- Contracts were frozen and hashed before any timed window:
  [baseline](contract-baseline.json) (48 pairs), [M7R](contract-m7r.json),
  [M2R](contract-m2r.json) and [combined](contract-combined.json) (96 pairs each).
  Every hash was rechecked by the [runner](tools/master.py) before its windows.
- **Window gate**, adapted to Windows ([`window.py`](tools/window.py)):
  - The GPU lock is a directory (`mkdir`/`rmdir`) shared with another agent's
    D3D12 work, held only for one window.
  - Under the lock, a 15 s pre-window sample must show all GPU engine time,
    including System, at most 2%, and foreign CPU at most 5%.
  - During the window, foreign GPU must be at most 2% of wall time. It comes
    from PDH `GPU Engine(*)\Utilization Percentage` per process and engine, for
    images other than `harness-*.exe` and System (PID 4).
  - System GPU time is recorded separately (`systemGpuPct`). It is WDDM
    video-memory paging and zeroing on the Copy engine for every process: 0.0%
    at idle and about 7.5-7.8% while harness processes run
    ([tool check](diagnostics/tool-check-3/gate.json)).
  - Foreign CPU must be at most 5% of window capacity (wall x 16 logical CPUs):
    GetSystemTimes busy minus the window tree (window script, cohort process
    and every harness child).
  - Every window also needs zero failed processes and the Dawn A/A check:
    warm-operation median in [0.95, 1.05] with the CI containing 1.
- **Windows background load.** Background processes are measured, not stopped:
  two idle `codex.exe` daemons, a `node` process and a desktop app belong to
  other owners and kept running. Windows Search indexing and Defender scanning
  started when the Dawn checkout landed in the profile; the first idle probe
  measured 15% foreign CPU ([probe 1](diagnostics/idle-probe-1/gate.json)).
  After they settled, a 60 s probe measured foreign CPU 0.22% and GPU 0.0%
  ([probe 2](diagnostics/idle-probe-2/gate.json)), and only then were the
  contracts frozen. Builds and GPU correctness runs were done only while
  holding the lock between windows.
- Adapter acquisition takes 3.5-4.4 s per process for both providers
  (`setupAdapterNs`). The loader enumerates the AMD ICD, a Dozen ICD whose
  `vkEnumeratePhysicalDevices` fails with -3, and three implicit layers. Doe's
  instance creation is 26 µs; the cost is host loader and driver
  initialization, so it is reported but not attributed to either provider.

## Baseline: Doe C cb7e56ecd against Dawn d04f88359 (48 pairs per workload)

All three windows passed the gate on attempt 1. Foreign GPU was 0.00% in each.
Foreign CPU was 3.31%, 2.07% and 1.92%, with the pre-window check at 0.0-0.55%
GPU. No process failed, and every iteration was oracle-checked. Dawn A/A warm
operation: 1.004 [0.999,1.011], 1.002 [0.996,1.011] and 0.993 [0.976,1.012].
[Summary](baseline/summary-vs-dawn.json).

Cells give the paired ratio Doe/Dawn [CI95]. Where shown, Doe / Dawn medians
come first, in ms. **Bold** marks a loss (CI lower bound above 1).

| Phase | vector 262k | image invert 1024 | upload/readback 1 MiB |
| --- | --- | --- | --- |
| setup ms | 3548 / 3580, 0.994 [0.986,0.999] | 3485 / 3505, 0.995 [0.991,0.998] | 3529 / 3543, 0.996 [0.993,0.999] |
| compile | 0.584 [0.573,0.630] | 0.573 [0.566,0.590] | (none) |
| pipeline | 0.754 [0.740,0.763] | 0.753 [0.744,0.762] | (none) |
| resources | 2.027 / 3.170, 0.643 [0.627,0.657] | 3.110 / 3.963, 0.777 [0.759,0.793] | 1.265 / 1.748, 0.726 [0.699,0.730] |
| teardown | **1.077 [1.046,1.111]** | 0.973 [0.937,0.997] | 0.926 [0.910,0.971] |
| first operation | **1.532 / 0.772, 2.017 [1.945,2.061]** | **2.263 / 2.217, 1.024 [1.013,1.032]** | 1.350 / 1.370, 0.974 [0.946,1.012] |
| warm upload | **1.100 [1.091,1.112]** | 0.300 [0.295,0.303] | **1.117 [1.099,1.226]** |
| warm encode | 0.579 [0.571,0.591] | 0.512 [0.494,0.552] | 0.667 [0.636,0.682] |
| warm submit | **1.592 [1.577,1.612]** | 0.165 [0.159,0.169] | **1.200 [1.187,1.219]** |
| warm completion | 0.854 [0.846,0.866] | **1.554 [1.510,1.582]** | 0.750 [0.744,0.752] |
| warm map | **1.034 [1.028,1.037]** | 1.001 [0.991,1.009] | **1.043 [1.040,1.052]** |
| warm operation ms | 0.299 / 0.306, 0.979 [0.970,0.984], 38/48 | 0.957 / 1.547, 0.621 [0.614,0.629], 48/48 | 0.225 / 0.247, 0.921 [0.905,0.943], 40/48 |
| warm p95 operation | 0.998 [0.971,1.008] | 0.606 [0.597,0.609] | 0.920 [0.905,0.949] |
| complete ms | 16.962 / 17.793, 0.955 [0.945,0.966] | 39.045 / 59.652, 0.652 [0.648,0.662] | 11.450 / 12.760, 0.902 [0.892,0.914] |
| process CPU | 0.980 [0.963,1.000] | 0.984 [0.975,1.000] | 1.000 [0.990,1.010] |
| peak working set | **1.007 [1.007,1.009]** | 0.884 [0.883,0.885] | **1.007 [1.005,1.008]** |

On this tuple, Doe C main is faster than Dawn in warm operation and `complete`
on all three workloads. The vector warm-operation margin is small (0.979, 38 of
48 pairs).

## Attribution: Doe and Dawn side by side (task 3)

Instruments, all untimed and outside every contract window:
- **Counting layer** ([source](tools/count_layer.c)). This is the amd-host
  round-3 layer, ported to Windows. It keys allocations by memory type index,
  because this driver's `DEVICE_COHERENT`/`UNCACHED` types share low flag bits
  with ordinary types. [Counts](diagnostics/vulkan-call-counts.jsonl) are per
  iteration: (33-iteration count − 1-iteration count) / 32.
- **Per-call timing.** The same layer with a QPC timer around each forwarded
  call ([times](diagnostics/vulkan-call-times.jsonl): 5 one-iteration and 3
  33-iteration runs per provider).
- **Medium integrity.** The ssh session runs at High integrity, and the Vulkan
  loader ignores `VK_ADD_LAYER_PATH` there. Both counting passes therefore ran
  through a duplicated token lowered to Medium integrity
  ([launcher](tools/medium.py)). No registry or ICD configuration was changed.
- **Probes.** Two Vulkan programs: [`memprobe`](tools/memprobe.c) times host
  `memcpy` and `memset` of 1 MiB into each host-visible memory type, and
  [`gpuprobe`](tools/gpuprobe.c) times a 4 MiB `vkCmdCopyBuffer` within types
  0, 2 and 3 ([results](diagnostics/memory-probes.jsonl)).

Memory types on this adapter: 0 `0x1` (heap 0, 11.4 GiB device-local);
1 `0x6` and 3 `0xe` (heap 1, 5.7 GiB system); 2 `0x7` (heap 2, 256 MiB
device-local host-visible).

| Buffer class | Doe main | M2R / c-direct | Dawn d04f88359 |
| --- | --- | --- | --- |
| Storage in/out, uniform | type 3 `0xe` (`resources.c:112-113`: HOST_VISIBLE\|COHERENT, prefer HOST_CACHED) | type 2 `0x7` | type 0 `0x1`, suballocated (`BufferVk.cpp:172-192` DeviceLocal kind; `MemoryTypeSelector.cpp:110-121` favors device-local, `:149` biggest heap) |
| writeBuffer source | `memcpy` into the mapped destination (`resources.c:185`, from `native/src/buffers.c:415-445`) | `memcpy` into type 2 | `DynamicUploader` staging in type 1 `0x6` (`DynamicUploader.cpp:108-118`); 4 MiB writes create a new ring buffer every iteration (`:96-104`), so 1 `vkCreateBuffer` + `vkAllocateMemory` + `vkMapMemory` per image iteration |
| MapRead readback | type 3, zero-copy map | type 3 | type 3 (`MemoryTypeSelector.cpp:129` prefers HOST_CACHED for read-mappable) |

| Per iteration (vector, image) | Doe main | M7R / c-direct | Dawn |
| --- | ---: | ---: | ---: |
| `vkCreateDescriptorPool` / `vkDestroyDescriptorPool` | 1 / 1 | 0 / 0 | 0 / 0 (sets made at bind-group creation, `BindGroupVk.cpp:57`, `DescriptorSetAllocator.cpp:175,190`) |
| descriptor allocate / free / update | 1 / 0 / 1 | 1 / 1 / 1 | 0 / 0 / 0 |
| `vkCmdPipelineBarrier` | 4 | 4 | 6 |
| `vkCmdCopyBuffer` | 1 | 1 | 2 |
| fence reset / wait / status | 1 / 1 / 1 | 1 / 1 / 1 | 0.97 / 1 / 2 |

This is the same structure as on RADV and ANV.

### Owners of the baseline losses

| Loss | Owner, evidence class |
| --- | --- |
| Vector warm submit 1.59x (+16 µs); copy 1.20x (+4 µs) | **Measured.** Doe records the whole Vulkan command buffer inside `wgpuQueueSubmit` (`native/src/queue.c:513`), like Dawn. Doe's extra cost has two parts. First, a descriptor pool is created and destroyed per dispatch use (`backends/vulkan/src/descriptors.c:100,255`): 5.5 + 1.8 µs per iteration in the timing layer. Second, four full `ALL_COMMANDS\|HOST` memory barriers (`commands.c:43-49`) cost 9.2 µs per iteration on vector, against 3.1 µs for Dawn's six precise buffer barriers (`CommandBufferVk.cpp:480,529`). On the copy workload Doe's two barriers cost 1.0 µs. M7R removes the pool calls (below). The barrier cost has no candidate here. |
| Image warm completion 1.55x (0.468 vs 0.301 ms) | **Measured as an accounting split, not GPU work.** Dawn's `wgpuQueueSubmit` issues `vkQueueSubmit` (`Queue.cpp:614` → `QueueVk.cpp:349`) and then calls `device->Tick()` (`Queue.cpp:620`). The tick releases completed upload ring buffers (`DynamicUploader.cpp:165-182`); `vkFreeMemory` costs 266 µs per image iteration in the timing layer, while the replacement 4 MiB staging allocation (386 µs) falls in `writeBuffer`, inside Dawn's 0.711 ms upload phase. The GPU runs during that time, so Dawn's completion phase starts later and looks shorter. Warm submit + completion per process, paired, favors Doe on all three workloads: vector 0.944 [0.937,0.951], image 0.861 [0.853,0.871], copy 0.808 [0.803,0.816]. Ruled out by measurement: placement and code generation (the captured Doe and Dawn image SPIR-V both dispatch in 200-205 µs over types 0, 2 and 3, [`dispatchprobe`](tools/dispatchprobe.c)); copy bandwidth by type (`gpuprobe`, 200-205 µs); M2R's type 2 (completion 0.995 vs main); barrier scope (M6, 0.976 [0.947,1.016]); and the upstream branch-free SPIR-V clamp (0.980 [0.960,0.994]) in the [image A/B diagnostic](diagnostics/image-ab2/). |
| Warm upload 1.10x (vector), 1.12x (copy), about +6 µs per MiB | **Measured path difference; cause inferred.** Doe's `memcpy` targets the cached destination, which the GPU read in the previous iteration. Dawn's targets uncached staging (type 1). In a hot loop, type 3 and type 1 `memcpy` cost the same (17.7 vs 19.2 µs per MiB, `memprobe`). *Inference, not verified:* the harness gap comes from refetching destination lines the GPU's coherent access evicted (read-for-ownership), which streaming writes to uncached memory avoid. |
| Vector first operation 2.0x (+0.76 ms), image 1.02x | **Measured as an accounting split.** The first `vkCreateDescriptorPool` on this driver costs about 500 µs for both providers (1-iteration timing). Dawn pays it in resources, at bind-group creation. Doe pays it inside its first dispatch submit (first submit 1.02 vs 0.21 ms). Dawn also creates its command pool and buffer at queue initialization (`QueueVk.cpp:93-97,202-214`); Doe creates its own at the first recording (`commands.c:70-93`). Resources + first operation: Doe 2.03 + 1.53 = 3.56 ms against Dawn 3.17 + 0.77 = 3.94 ms. |
| Vector teardown 1.08x | **Measured split.** Doe frees each allocation at buffer release (`resources.c:152-157`; `vkFreeMemory` 0.72 ms in a one-iteration vector run). Dawn defers through its fenced deleter (`BufferVk.cpp:770,775`) and suballocates. Teardown release is 0.91 vs 0.08 ms and device destruction 1.33 vs 2.02 ms. |
| Warm map 1.03-1.04x (+1 µs) | **Not attributed.** Both read the same type 3 memory. Doe lends the mapped range (amd-host M1); the remaining microsecond is frontend overhead and was not traced. |
| `setupDeviceNs` 1.04x (+0.4 ms) | **Not attributed.** Adapter acquisition (3.5 s) dominates setup for both providers and is host-owned. |

## Frozen amd-host candidates as new windows-amd-host contracts (task 4)

Each candidate is its amd-host receipt patch applied unchanged to `cb7e56ecd`;
every patch applied without conflict. Each acceptance rule is copied unchanged
from the amd-host round-4 contract. Pair counts are 96, set from this host's
timing budget; they are not a power simulation. Providers per window: main,
candidate, Dawn and `dawn-aa`, over all 24 orders. These are new prospective
contracts, not reruns.

| Contract | Verdict | Decisive results (candidate / main) |
| --- | --- | --- |
| [M7R](contract-m7r.json), rule of `amd-vulkan/.../round4/frozen-m7r2.json` | **PASS** ([gate](m7r/gate.out)) | complete: vector 0.981 [0.974,0.993], image 0.991 [0.981,0.995], upload/readback 1.006 [1.000,1.012] ≤ 1.02; warm submit: vector 0.876 [0.871,0.882], image 0.881 [0.868,0.893]; no warm phase-guard violation |
| [M2R](contract-m2r.json), rule of `round4/frozen-m2r2.json` | **FAIL** (primary) ([gate](m2r/gate.out)) | complete: 1.023 [1.019,1.029], 1.052 [1.043,1.061], 1.032 [1.027,1.038]. The allowances also fail: upload medians 1.27-1.28 and resources 1.20-1.24 exceed 1.15. The separate upload-latency gate fails. |
| [Combined, c-direct](contract-combined.json), rule of `round4/frozen-combined.json` | **FAIL** ([gate](combined/gate.out)) | complete: 1.017 [1.009,1.022], 1.041 [1.033,1.048], 1.028 [1.020,1.041]; allowances fail (upload 1.19-1.28, resources 1.19-1.25) |
| Combined under the owner's [AMD trade-off rule](../../amd-vulkan/20261029-dropin-phase/landing-c-direct-amd.json), declared in the contract before timing | **FAIL** ([gate](combined/tradeoff-gate.out)) | It fails `complete` vs main, warm completion vs main (image 0.995 [0.988,1.009]), the accepted trade-off (upload median 1.27 > 1.12) and the phase guard (resources). It passes `complete` vs Dawn (0.958, 0.675, 0.920) and warm submit vs main (0.688, 0.743). |

### M7R / main and against Dawn

| m7r / main | complete | warm submit | warm completion | warm upload | warm operation | resources | teardown |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 0.981 [0.974,0.993] | 0.876 [0.871,0.882] | 0.990 [0.981,0.996] | 1.003 [0.998,1.007] | 0.977 [0.972,0.984] | 0.993 [0.981,1.006] | 0.996 [0.983,1.016] |
| image | 0.991 [0.981,0.995] | 0.881 [0.868,0.893] | 0.981 [0.965,0.990] | 0.993 [0.985,1.000] | 0.979 [0.962,0.990] | 0.997 [0.987,1.007] | 1.032 [1.003,1.043] |
| upload/readback | 1.006 [1.000,1.012] | 1.004 [1.000,1.017] | 1.003 [0.995,1.011] | 1.000 [0.995,1.007] | 1.002 [0.998,1.006] | 0.993 [0.988,1.009] | 0.996 [0.969,1.014] |

| m7r / Dawn | complete | warm operation | warm p95 | warm submit | warm completion | CPU | peak working set |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 0.922 [0.913,0.931] | 0.947 [0.941,0.951] | 0.927 [0.911,0.951] | 1.396 [1.383,1.401] | 0.835 [0.831,0.839] | 0.995 [0.985,1.005] | 1.008 [1.007,1.009] |
| image | 0.642 [0.638,0.647] | 0.604 [0.594,0.614] | 0.598 [0.583,0.609] | 0.137 [0.135,0.139] | 1.524 [1.497,1.556] | 0.990 [0.983,1.000] | 0.884 [0.884,0.885] |
| upload/readback | 0.901 [0.888,0.907] | 0.904 [0.898,0.911] | 0.910 [0.879,0.930] | 1.192 [1.185,1.200] | 0.748 [0.743,0.752] | 0.990 [0.985,1.000] | 1.007 [1.006,1.009] |

M7R removes the per-submit pool calls and passes. Vector submit is still
1.40x Dawn: the four full-scope barriers remain (`commands.c:43-49`, 9.2 vs
3.1 µs). M6, which narrows their scopes, does not change image completion here
(0.976). Its vector submit effect was not measured.

### Why M2R, and therefore c-direct, loses on this tuple

The cause is measured. On RADV, M2R's host-visible device-local type sits in
the large device heap and cut completion to 0.68-0.88 of main. On this APU, the
first type that is `HOST_VISIBLE|HOST_COHERENT` and `DEVICE_LOCAL` is type 2,
in heap 2: a 256 MiB window. `memprobe` shows host writes there are 4.6x slower
than to Doe main's type 3: `memcpy` of 1 MiB takes 82 against 17.7 µs, and
`memset` 111 against 9.7 µs. GPU access is no faster: a 4 MiB copy takes
200-205 µs in either type, and the image dispatch the same (`dispatchprobe`).
So M2R pays in upload (1.20-1.28x) and in resource zeroing (1.20-1.24x,
`resources.c:136`) and gains almost nothing in completion (0.93-1.02). Its
submit gain (0.79-0.98) was not traced.

### M2R and combined tables

| m2r / main | complete | warm submit | warm completion | warm upload | warm operation | resources | teardown |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 1.023 [1.019,1.029] | 0.793 [0.790,0.798] | 0.981 [0.974,0.987] | 1.274 [1.266,1.279] | 1.018 [1.013,1.022] | 1.242 [1.232,1.259] | 0.998 [0.983,1.005] |
| image | 1.052 [1.043,1.061] | 0.835 [0.823,0.843] | 1.022 [1.016,1.036] | 1.201 [1.190,1.214] | 1.046 [1.039,1.056] | 1.223 [1.195,1.230] | 0.897 [0.874,0.916] |
| upload/readback | 1.032 [1.027,1.038] | 0.977 [0.971,0.987] | 0.927 [0.921,0.935] | 1.283 [1.278,1.286] | 1.043 [1.038,1.046] | 1.198 [1.177,1.212] | 1.018 [0.991,1.032] |

| combined / main | complete | warm submit | warm completion | warm upload | warm operation | resources | teardown |
| --- | --- | --- | --- | --- | --- | --- | --- |
| vector | 1.017 [1.009,1.022] | 0.688 [0.684,0.691] | 0.973 [0.967,0.979] | 1.272 [1.264,1.279] | 0.994 [0.988,0.998] | 1.249 [1.226,1.267] | 1.020 [1.002,1.033] |
| image | 1.041 [1.033,1.048] | 0.743 [0.733,0.761] | 0.995 [0.988,1.009] | 1.190 [1.182,1.200] | 1.031 [1.028,1.037] | 1.221 [1.198,1.235] | 0.900 [0.880,0.927] |
| upload/readback | 1.028 [1.020,1.041] | 0.979 [0.973,0.984] | 0.928 [0.923,0.938] | 1.276 [1.265,1.285] | 1.033 [1.026,1.040] | 1.191 [1.180,1.210] | 1.015 [1.008,1.038] |

Against Dawn, combined still wins `complete` (0.958, 0.675, 0.920) and warm
operation (0.972, 0.639, 0.941). It is slower than main.

### Windows and their validity

Twelve contract windows ran. Each attempt is retained with `gate.json` and
`validity.json`.
- Every counted window was valid. Foreign GPU was 0.000-0.001% and foreign CPU
  1.88-3.31%. System (paging) GPU time was 7.5-8.1% on the Copy engine.
- A/A medians were 0.992-1.004, each CI containing 1.
- Two attempts were **invalid**, rerun fresh and never used:
  - M2R image attempt 1: A/A CI [1.0008,1.0079] excluded 1.
  - M2R image attempt 2: pre-window foreign CPU was 5.10%.

## Owner decision on landing c-direct for AMD: implemented and gated, not pushed

The coordinator relayed the owner's decision: land c-direct selected per
adapter for AMD only, under an explicit trade-off rule. The rule is written as
[`landing-c-direct-amd.json`](../../amd-vulkan/20261029-dropin-phase/landing-c-direct-amd.json).
It was frozen before any windows-amd-host candidate window, it references round
5, and round 5 passes it.

The implementation is [`landing/per-adapter-resource-profile.patch`](landing/per-adapter-resource-profile.patch),
against `b558c1456`:
- `doe_vk_caps.resource_profile` is chosen once in `device_selection.c` from the
  selected adapter's `vendorID`: `0x1002` selects AMD; everything else selects
  DEFAULT.
- Descriptor and placement code branch only on that field.
- DEFAULT keeps the per-use exact pool and host-cached placement, call for call.
- The rule is recorded in the Vulkan backend `AGENTS.md` and `CATSCAN.md`.
- New tests: `ownership.c` checks placement per profile, and the three
  descriptor and lifetime mocks also build as `_amd` variants.

Correctness, landing against main on the same tree:

| Host | Checks | Result |
| --- | --- | --- |
| Intel Linux host (ANV), `cb7e56ecd` and `b558c1456` bases | `make -C runtime/c check`; all Vulkan owner contracts individually; 80 physical clients; `check-vulkan` and the native software check on lavapipe (process-local ICD); counting layer on all three workloads | Identical to main. Physical: 80/80 on both. Owner contracts: only `surface_features` and `feature_contract` segfault, on main too. `_amd` variants pass. Software: same 56 PASS and the same abort. Counting layer: call-for-call identical (per-dispatch pools, memory type 2 `0xf`), except fence-poll counts, which vary between runs of main itself (33-53 waits). |
| windows-amd-host (AMD Vulkan), both bases | 71 public-ABI physical clients built with clang-cl, run with `DOE_C_TEST_BACKEND=vulkan` | Identical: 67 pass and the same 4 fail on main and landing (below) |
| amd-host RADV | confirmation of the landed build | **Pending, not run** (that host is not used by this task) |

**Why it is not pushed:** on windows-amd-host, `vendorID 0x1002` selects
c-direct, and c-direct is measurably worse than main there. `complete` is +1.7%,
+4.1% and +2.8%. It fails the owner's rule on this tuple, and the cause is the
M2R placement, not M7R. Pushing would ship a regression measured by the rule
meant to accept it. The concrete options are for the owner:

1. **Push as decided** (vendor `0x1002` → M7R + M2R). RADV keeps its round-5
   gains. Windows AMD Vulkan, which is not listed in the support matrix (and
   is not the Windows default backend, D3D12), takes the regression above.
2. **M7R for vendor `0x1002`; M2R only where its device-local host-visible
   type is not a small window.** Example: require the selected type's heap to
   exceed 256 MiB, the non-ReBAR window that a Dawn TODO names
   (`MemoryTypeSelector.cpp:60-61`; Dawn does not yet apply it). Or select M2R by the RADV `driverID`.
   This is a changed rule; it needs its own frozen evidence on each AMD tuple
   before landing.
3. **M7R only for vendor `0x1002`**, which passes on both AMD tuples measured so
   far (amd-host round 5, windows-amd-host). M2R stays a receipt.

## Windows Vulkan gaps found (not attributed, not fixed)

On both main and landing, 4 of 71 public-ABI clients fail on this tuple:
- `compute-programs`: `doeNativeComputeProgramSupported() == 1` fails at
  `native/tests/compute_programs.c:198`.
- `limits`, `occlusion` and `workgroups` exit with `0xC0000409` and print
  nothing. That is the fast-fail status, not an `assert`, which would print.

`iosurface-metadata` and `iosurface-shared` are Darwin-only and do not build.
These are first Windows Vulkan observations, outside this record's scope.

## Remaining owners on windows-amd-host (baseline)

1. **Vector warm submit (1.59x), copy submit (1.20x).** Per-dispatch
   descriptor pools (M7R removes them; passes) and full-scope barriers (not
   addressed). Submit + completion together still favor Doe (0.944, 0.808).
2. **Warm upload (1.10-1.12x).** Doe writes the cached destination directly;
   Dawn streams into uncached staging. The read-for-ownership cause is
   inferred.
3. **First operation (vector 2.0x)** and **teardown (vector 1.08x).**
   Accounting splits; their sums favor Doe.
4. **Warm map (1.03-1.04x)** and **`setupDeviceNs` (1.04x).** Not attributed.
5. **Adapter acquisition, 3.5-4.4 s for both providers.** Host loader and
   driver cost.

Nothing here is the GOALS application win, a public claim, or qualification of
this tuple.
