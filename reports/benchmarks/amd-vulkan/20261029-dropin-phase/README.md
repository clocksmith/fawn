# AMD Vulkan drop-in phase attribution (C1)

Consumer: proof 1 (current Doe against Dawn on the initial qualification
target). The record attributes Doe C losses against a source-built Dawn control
by phase. It also retains the accepted and rejected runtime mechanisms. It is a
**scoped native drop-in diagnostic, not the GOALS Vulkan application win**,
which needs a complete unchanged application.

## Campaign continuity

The previous Node5 package baseline (`package_*` vector, image, upload/readback,
pipeline creation and queue completion; C, Zig and npm Dawn) never ran. Three
attempts stopped before timing because graceful participant quiescence failed.
This cohort keeps three Node5 workloads and their WGSL, generators, seed-0
inputs and frozen capture oracles. It moves to the public `webgpu.h` ABI, so
the Dawn control is the same harness source linked against source-built Dawn
rather than an npm binary. The pipeline-creation and queue-completion workloads
are not run here. Their costs appear only as the `pipeline` and `completion`
phases, and they remain unmeasured as Node5 rows.

## Method

- Host: amd-host, initial qualification target (Radeon 8060S, RADV Mesa 26.0.3).
  Every process asserts a physical Vulkan adapter. All providers reported
  `Radeon 8060S Graphics (RADV STRIX_HALO)`.
- Harness: [`bench/drop-in/dropin_phase_harness.c`](../../../../bench/drop-in/dropin_phase_harness.c).
  One source is linked per provider through
  [`bench/tools/dropin_phase_cohort.py`](../../../../bench/tools/dropin_phase_cohort.py).
- Providers: Doe C `libwebgpu_doe_c.so` at source `35018c4a3`, and Dawn
  `d04f88359` Release. `dawn-aa` is a byte-identical Dawn harness copy that
  serves as the A/A control. Retained Zig is recorded **unsupported** for this
  harness: `wgpuCreateInstance` rejects the `TimedWaitAny` instance feature and
  fails closed. It is not timed.
- One-time phases: setup (instance, adapter, device and queue), compile (WGSL
  module), pipeline (auto layout), resources (buffers, parameters and bind
  group), and teardown (explicit device destroy).
- Per-iteration phases: upload (`writeBuffer`), encode (clear, dispatch, copy and
  finish), submit, completion (`onSubmittedWorkDone` through a timed `WaitAny`),
  and map (`mapAsync`, mapped copy and unmap). `operation` covers upload through
  map.
- Each process runs 32 iterations. Iteration 0 is reported as `first`, and
  iterations 1 to 31 are warm. Input sets alternate, so stale output cannot pass.
  The independent CPU oracle checks every iteration after its operation interval
  closes. The set-0 oracles match the frozen Node5 capture oracles exactly.
- Sampling: 24 balanced pairs per workload, cycling through all provider
  permutations. Each comparison reports a per-pair ratio median, a bootstrap 95%
  CI (10000 resamples, seed 20261029) and the count of pairs the provider won.
  Timing is host `CLOCK_MONOTONIC`, not GPU timestamps.
- Host control: each window ran under the shared GPU timing lock with the four
  participant services stopped. An EXIT trap restarted them, and all four were
  active after every window. Idle desktop and browser processes still held the
  render node; see `*/gpu-holders.json`.

Frozen contracts: [`contract-c1.json`](contract-c1.json) and
[`contract-m1.json`](contract-m1.json). They were written and hashed before
their windows ran.

## Baseline (Doe C 35018c4a3 / Dawn), medians, ratio [CI95]

| Phase | vector 262k | image invert 1024 | upload/readback 1 MiB |
| --- | --- | --- | --- |
| setup ms | 29.45 / 22.46, 1.34 [1.26,1.38] | 29.75 / 21.37, 1.43 [1.35,1.46] | 28.56 / 20.35, 1.44 [1.28,1.49] |
| compile | 0.35 [0.28,0.39] | 0.31 [0.28,0.32] | (none) |
| pipeline | 0.69 [0.66,0.74] | 0.86 [0.76,0.88] | (none) |
| resources | 0.53 [0.50,0.59] | 1.63 [1.34,1.78] | 1.92 [1.89,2.07] |
| warm upload | 0.75 [0.70,0.85] | 0.27 [0.25,0.29] | 0.97 [0.90,1.06] |
| warm submit | 1.61 [1.47,1.83] | 0.73 [0.68,0.80] | 0.98 [0.89,1.07] |
| warm completion | 1.34 [1.28,1.40] | 1.63 [1.52,1.74] | 1.17 [1.14,1.22] |
| warm map | 1.74 [1.69,1.90] | 2.03 [1.80,2.10] | 1.99 [1.67,2.15] |
| warm operation ms | 0.220 / 0.165, 1.32 [1.27,1.37] | 0.545 / 0.694, 0.79 [0.76,0.83] | 0.178 / 0.142, 1.24 [1.16,1.31] |
| warm p95 operation | 1.15 [1.04,1.32] | 0.89 [0.75,0.96] | 1.23 [1.14,1.29] |
| process CPU | 1.10 [1.04,1.14] | 0.96 [0.95,1.02] | 1.12 [1.05,1.21] |
| peak RSS | 0.90 | 0.92 | 0.94 |

The A/A warm-operation ratios were 0.957, 1.000 and 1.005, and every CI
contained 1. Raw data and the summary are in `baseline/`.

## M1, accepted: lend the Vulkan host range to read mappings

Every Doe C Vulkan buffer already lives in persistently mapped, coherent and
preferably cached host memory. `mapAsync` still allocated a host shadow, copied
into it, and returned that copy. A READ mapping exposes only const ranges.
Submission, binding and queue writes already refuse mapped buffers. M1 therefore
lends the backend range after the existing settle, under the same
lost-device, busy and poisoned-content admission as a read. Write mappings,
mapped-at-creation, Metal and D3D12 keep their copies. See
[`m1/candidate.patch`](m1/candidate.patch).

Focused correctness on the candidate library: all 74 physical-Vulkan native
clients passed, the software native `check` passed (115 PASS), `check-vulkan`
on lavapipe passed, and the C style, tools and mutable-globals checks passed.
The `make -C runtime/c check` aggregate could not run on the exFAT work volume
because `git` and symlinks are unavailable there. Its parts were run separately.

| doe-c-m1 / doe-c | vector | image | upload/readback |
| --- | --- | --- | --- |
| warm map | 0.53 [0.53,0.58] | 0.53 [0.51,0.56] | 0.52 [0.51,0.53] |
| warm operation | 0.91 [0.88,0.94] | 0.84 [0.83,0.91] | 0.89 [0.86,0.91] |
| warm p95 operation | 0.86 [0.74,0.99] | 0.82 [0.75,0.93] | 0.90 [0.86,0.94] |
| first operation | 0.77 [0.70,0.82] | 0.74 [0.69,0.80] | 0.80 [0.76,0.84] |

The frozen mechanism rule passes on all three workloads. The largest other
ratio was image warm completion at 1.06 [1.03,1.14]. Its lower bound is below
the 1.05 regression threshold.

### M1 against Dawn, with the frozen scoped rule

| Workload | warm operation | warm p95 | CPU | RSS | Scoped rule |
| --- | --- | --- | --- | --- | --- |
| image invert 1024 | 0.68 [0.66,0.70], 24/24 | 0.66 [0.60,0.70] | 0.91 | 0.91 | **pass** |
| vector 262k | 1.31 [1.26,1.34], 0/24 | 1.27 [1.19,1.36] | 1.07 | 0.92 | fail |
| upload/readback 1 MiB | 1.07 [1.04,1.13], 2/24 | 0.98 [0.91,1.06] | 1.06 | 0.95 | fail |

The one-time regression bound fails on all three workloads. Setup takes about
1.33x Dawn's time, roughly 7 ms per process.

## M2, rejected: GPU-only buffers in device-local memory

M2 built on M1. Buffers without map usage kept the coherent persistent mapping
but preferred device-local memory; mappable buffers stayed host-cached. See
[`m2/candidate.patch`](m2/candidate.patch) and
[`contract-m2.json`](contract-m2.json). Its focused correctness checks passed:
the same 74 physical clients, the software check (115 PASS) and `check-vulkan`.

| doe-c-m2 / doe-c-m1 | vector | image | upload/readback |
| --- | --- | --- | --- |
| warm completion | 0.84 [0.82,0.86] | 0.74 [0.73,0.75] | 0.84 [0.83,0.85] |
| warm operation | 0.91 [0.88,0.92] | 0.86 [0.84,0.88] | 0.91 [0.88,0.93] |
| warm upload | **1.11 [1.10,1.14]** | 1.02 [0.98,1.04] | 1.01 [0.98,1.10] |

**Rejected under the frozen rule.** Vector warm upload regressed: the lower CI
bound exceeds 1.05, because host writes now go to write-combined device-local
memory. The completion and operation gains are real and remain recorded, but
the frozen rule takes precedence over them. Any successor must keep upload
within the bound or be frozen as a separate trade-off decision. Against Dawn,
M2 reached warm operation 0.56x (image), 1.16x (vector) and 1.05x
(upload/readback).

## M3, accepted: device contexts share the adapter's Vulkan instance

A harness revision split setup into instance, adapter and device inside the
unchanged setup interval. An untimed diagnostic showed adapter cost equal for
both providers (about 21 ms). `requestDevice` took 9.5 ms on Doe and 1.5 ms on
Dawn. An `openat` trace showed Doe loading the loader, creating a second
`VkInstance` and enumerating ICDs and the render node a second time for each
device.

M3 gives the adapter context and its device contexts one loader handle and
`VkInstance` through an atomically reference-counted owner. Identity selection
still runs on that instance, and the native identity comparison is unchanged.
A source with different surface platforms is refused. See
[`m3/candidate.patch`](m3/candidate.patch) and
[`contract-m3.json`](contract-m3.json).

The extended `native_compute` covers the new lifetime. The adapter is released
before device work, and mismatched sharing is refused. It passed on RADV and
lavapipe. The other focused checks also passed: 74 physical clients, the
software check (115 PASS) and `check-vulkan`. Vulkan backend contracts match
M1: `surface_features` and `feature_contract` segfault on both M1 and M3
(pre-existing), and every other contract passes.

| doe-c-m3 / doe-c-m1 | vector | image | upload/readback |
| --- | --- | --- | --- |
| setup, device | 0.08 [0.07,0.08] | 0.08 [0.08,0.08] | 0.07 [0.07,0.08] |
| setup | 0.66 [0.64,0.70] | 0.74 [0.70,0.76] | 0.69 [0.67,0.72] |
| teardown | 0.60 [0.55,0.63] | 0.64 [0.62,0.66] | 0.55 [0.52,0.61] |
| process wall | 0.81 [0.79,0.85] | 0.92 [0.90,0.93] | 0.84 [0.80,0.85] |
| process CPU | 0.82 [0.78,0.85] | 0.91 [0.89,0.93] | 0.82 [0.79,0.83] |
| first operation | 0.98 [0.89,1.09] | 0.97 [0.91,1.03] | **1.18 [1.08,1.27]** |

The frozen mechanism rule passes. One disclosed side effect falls outside the
rule's named phases: the copy workload's first operation is slower than M1
(0.84 ms against 0.73 ms). It is retained as an open observation.

### Current state against Dawn (M1 + M3), with the frozen scoped rule

| Workload | warm operation | warm p95 | setup | process wall | CPU | RSS | Scoped rule |
| --- | --- | --- | --- | --- | --- | --- | --- |
| image invert 1024 | 0.65 [0.62,0.66], 24/24 | 0.66 [0.57,0.70] | 0.99 | 0.84 | 0.80 | 0.91 | **pass** |
| vector 262k | 1.29 [1.26,1.31], 0/24 | 1.24 [1.17,1.30] | 0.95 | 0.92 | 0.90 | 0.92 | fail |
| upload/readback 1 MiB | 1.07 [1.04,1.08], 3/24 | 0.97 [0.89,1.07] | 0.92 | 0.87 | 0.86 | 0.95 | fail |

The one-time regression bound now passes on all three workloads; it failed at
the baseline. A/A ratios stay within the frozen window.

## Remaining owners of loss (M1 + M3 against Dawn)

1. Warm completion: 1.13x (copy), 1.38x (vector) and 1.74x (image). M2 shows
   memory placement owns most of this. Any accepted successor must not regress
   host upload.
2. Warm submit on vector: 1.70x [1.58,1.84].
3. Resources for buffer creation: 1.67-1.88x on image and copy.
4. Map on image: 1.05x.

Retained Zig remains unmeasured on this harness because it refuses
`TimedWaitAny`. Losses and failed candidates stay retained here. Neither a pass
on a single workload nor the rules above promotes a public claim, and none of
this is the GOALS application win.
