# Fawn 158 vs stock Chromium 158 on WebGPU pages

Disposition: **diagnostic comparison**. On macOS, Fawn with forced Doe produced
correct output on every page and launch. It loses on GPU-process CPU, memory and
frame completion latency on every page. Most of the memory loss is a
presentation-path leak of about one canvas-sized allocation per frame. Windows
(D3D12) results will be added in this directory when that build exists; this
commit contains macOS only.

## What was compared

| Arm | Browser | Runtime selection |
|---|---|---|
| `chrome` | Chrome for Testing 158.0.8108.0 (official build) | stock Dawn |
| `fawn_doe` | Fawn 158.0.8108.0 release candidate (`Contents/MacOS/Chromium` launcher) | `--use-webgpu-runtime=doe` plus the bundled `libwebgpu_doe_c.dylib` |
| `fawn_dawn` | same Fawn binary | `--use-webgpu-runtime=dawn` (same-binary control) |

- Host: mac-host, Apple M3 (10-core GPU, 24 GiB), macOS 26.6.2, built-in 60 Hz
  display, device pixel ratio 2.
- Stock and Fawn share the same Chromium version, 158.0.8108.0. The stock build
  is an official PGO build; Fawn is `is_official_build=false` with
  `chrome_pgo_phase=0`. The `fawn_dawn` arm separates Doe's effect from
  that build difference.
- Fawn archive `fawn-doe-158.0.8108.0-macos-arm64.zip`, sha256
  `f850945841f49b13c2c784151cea16e7ddef5a37c2d88d0069c23ffc31ccf13a`. Built
  from Doe `d2e112822`, Chromium `4a6fb38567da`, Dawn `758e5e6a974e`. Its
  clean-install verifier passed strict forced-Dawn and forced-Doe smoke
  (`activeRuntimeProof.matched` on both).
- Doe runtime sha256 `ed9d41828648d103e388a15f419f767eb90ed7b56d4d8e1c6aac09e47001f363`,
  compiler `c1c11e75e729433ecef1e1a44ebb7c4db1cf0430e1dc8b862de02a40ac127cb2`.
  Fawn `Chromium-real` sha256 `728f6f7d8e9e…`. Chrome for Testing executable
  sha256 `2acf677e0b33…`.
- Pages: `browser/chromium/resources/` served unchanged from Doe `d2e112822`.
  Per-request sha256 values are recorded in each launch receipt.

## Method

- Every sample is a fresh, headed launch with a fresh profile and sandbox on.
  Each launch runs one page. Arms within a page alternate through all six
  orderings, so each ordering ran twice in 12 repetitions. That gives 12 launches
  per arm and page, 216 launches in total. Every launch held
  `flock /tmp/doe-gpu-timing.lock`, which interleaved with another Metal timing
  job.
- An init-script observer wraps `requestAdapter`, `requestDevice`,
  `GPUQueue.submit`, `onSubmittedWorkDone` and `getCurrentTexture`. Page
  code is not modified.
  - **Cold first job** = navigation start to the first completed submission
    that acquired a canvas texture. For the compute page it is navigation start
    to its verified oracle readback.
  - **Frame/op completion**:
    - animated pages: the time from `submit` to the resolution of the page's
      own `onSubmittedWorkDone`, per presented frame, over a 6 s window
      following a 2 s warm-up;
    - image lab: 60 slider updates, measured from the input event to completion;
    - compute page: 30 operations of 100 `workgroup_atomic` dispatches each,
      followed by a present.
- **CPU** is the `SystemInfo.getProcessInfo` CPU-time delta over the window,
  per frame or operation. **Memory** is macOS `footprint` at the end of the
  window: GPU-process phys_footprint, its graphics-tagged part, and the total
  footprint of all browser processes.
- **Provider**: `lsof` of the GPU process confirmed that
  `libwebgpu_doe_c.dylib` and `libdoe_compiler.dylib` were mapped in all 72
  `fawn_doe` launches and in none of the 144 others. The adapter was
  `apple / metal-3` in every launch.
- **Correctness**:
  - `compute_atomic_canvas` checks the repo's exact `sha256_exact_v1` oracle
    (`examples/workgroup_atomic_commands.json`) after the first job and again
    after the timed operations. It matched in 36 of 36 launches.
  - `compute_atomic_canvas` and `image_lab` are deterministic. Their
    screenshot RGBA hashes were identical across all launches and all three
    arms.
  - The animated pages are time-dependent, so they have a visual non-blank
    check only; their median luma matched across arms.
- Statistics: per-launch values; median (or nearest-rank p95) across launches.
  95% percentile bootstrap CIs use 10,000 resamples of launches, and ratio CIs
  resample each arm independently. A metric is a **win** or **loss** only when
  its ratio CI excludes 1.0 and the point effect is at least 3%. With 12
  launches, a p95 across launches is close to the maximum and is fragile.
- `compute_atomic_canvas` is a harness page, not a repo page. It runs the repo's
  `compute_workgroup_atomic_1024` L1 row (kernel, shape, 100-dispatch repeat,
  buffer, zero initialization and expected digest, all read from repo files) and
  presents the result buffer to a WebGPU canvas. The harness source is in the
  raw-evidence archive under `macos/harness/`.

Totals: 216 of 216 launches completed with no failures, no fallback, no device
loss and no uncaptured errors. Every launch's GPU work drained (`gpuCompletion`
completed).

## macOS results (Fawn+Doe vs Chrome 158)

#### compute_atomic_canvas

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 103 [97.8, 121] | 112 [105, 115] | 1.08 [0.90, 1.16] | no clear difference | 0.97 [0.80, 1.03] |
| Cold first job p95 (ms) | 138 [121, 138] | 136 [115, 136] | 0.99 [0.85, 1.07] | no clear difference | 0.81 [0.77, 0.89] |
| Frame/op completion p50 (ms) | 34.8 [33.8, 35.1] | 36.5 [36.4, 37.5] | 1.05 [1.04, 1.09] | loss | 1.01 [0.98, 1.04] |
| Frame/op completion p95 (ms) | 36.1 [35.9, 37.1] | 38.6 [38.3, 39.2] | 1.07 [1.04, 1.09] | loss | 1.01 [0.97, 1.02] |
| Completed-frame interval p50 (ms) | 49.6 [49.4, 49.8] | 49.8 [49.7, 49.9] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.00] |
| GPU-process CPU per frame/op (ms) | 3.42 [3.11, 4.14] | 7.69 [7.05, 8.77] | 2.25 [1.76, 2.66] | loss | 1.39 [0.96, 1.59] |
| All-process CPU per frame/op (ms) | 5.60 [5.10, 6.71] | 11.3 [10.0, 12.8] | 2.01 [1.52, 2.38] | loss | 1.48 [0.99, 1.69] |
| GPU-process memory (MB) | 195 [194, 199] | 724 [722, 727] | 3.72 [3.63, 3.74] | loss | 0.99 [0.97, 1.02] |
| Total browser memory (MB) | 361 [358, 364] | 890 [888, 892] | 2.47 [2.44, 2.49] | loss | 1.00 [0.99, 1.02] |

#### cycle

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 183 [172, 208] | 233 [228, 236] | 1.27 [1.10, 1.36] | loss | 1.03 [0.90, 1.10] |
| Cold first job p95 (ms) | 218 [208, 218] | 261 [236, 261] | 1.19 [1.09, 1.24] | loss | 0.91 [0.88, 0.94] |
| Frame/op completion p50 (ms) | 12.7 [12.4, 14.0] | 17.5 [15.0, 18.0] | 1.38 [1.18, 1.44] | loss | 1.02 [0.91, 1.06] |
| Frame/op completion p95 (ms) | 20.0 [16.1, 21.2] | 21.9 [21.8, 22.3] | 1.10 [1.03, 1.37] | loss | 1.06 [0.97, 1.32] |
| Completed-frame interval p50 (ms) | 17.6 [16.8, 19.1] | 25.7 [19.8, 29.9] | 1.46 [1.13, 1.74] | loss | 1.06 [0.95, 1.11] |
| GPU-process CPU per frame/op (ms) | 3.23 [2.80, 3.71] | 6.50 [6.15, 6.71] | 2.01 [1.66, 2.38] | loss | 1.09 [0.85, 1.28] |
| All-process CPU per frame/op (ms) | 4.94 [4.30, 5.70] | 8.94 [8.32, 9.21] | 1.81 [1.51, 2.12] | loss | 1.21 [0.93, 1.43] |
| GPU-process memory (MB) | 430 [429, 431] | 5701 [5667, 5916] | 13.26 [13.18, 13.78] | loss | 1.00 [1.00, 1.00] |
| Total browser memory (MB) | 631 [629, 633] | 5901 [5848, 6122] | 9.36 [9.27, 9.73] | loss | 1.00 [1.00, 1.01] |

#### heavy_particles

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 80.7 [60.3, 102] | 79.0 [77.4, 79.6] | 0.98 [0.77, 1.32] | no clear difference | 0.85 [0.67, 1.14] |
| Cold first job p95 (ms) | 115 [102, 115] | 84.8 [79.6, 84.8] | 0.74 [0.69, 0.83] | win | 0.66 [0.62, 0.74] |
| Frame/op completion p50 (ms) | 4.00 [3.90, 4.10] | 5.50 [5.40, 5.70] | 1.38 [1.34, 1.43] | loss | 0.98 [0.95, 1.00] |
| Frame/op completion p95 (ms) | 5.10 [5.00, 5.20] | 6.50 [6.40, 7.10] | 1.27 [1.23, 1.40] | loss | 1.02 [0.98, 1.04] |
| Completed-frame interval p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.00] | no clear difference | 1.00 [1.00, 1.00] |
| GPU-process CPU per frame/op (ms) | 2.28 [2.07, 2.55] | 5.52 [5.17, 5.96] | 2.43 [2.08, 2.70] | loss | 1.09 [0.92, 1.27] |
| All-process CPU per frame/op (ms) | 3.34 [3.03, 3.70] | 6.63 [6.22, 7.10] | 1.99 [1.73, 2.28] | loss | 1.16 [1.01, 1.36] |
| GPU-process memory (MB) | 276 [275, 277] | 7788 [7786, 7802] | 28.24 [28.16, 28.31] | loss | 1.00 [0.99, 1.00] |
| Total browser memory (MB) | 455 [455, 457] | 7968 [7967, 7982] | 17.49 [17.44, 17.54] | loss | 1.00 [0.99, 1.00] |

#### image_lab

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 56.5 [53.5, 85.0] | 71.6 [69.5, 77.0] | 1.27 [0.82, 1.38] | no clear difference | 1.00 [0.64, 1.16] |
| Cold first job p95 (ms) | 111 [85.0, 111] | 87.2 [77.0, 87.2] | 0.79 [0.69, 1.02] | no clear difference | 0.67 [0.56, 0.86] |
| Frame/op completion p50 (ms) | 16.6 [16.5, 16.6] | 16.6 [16.6, 16.6] | 1.00 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| Frame/op completion p95 (ms) | 18.5 [18.4, 19.0] | 18.0 [17.6, 18.2] | 0.97 [0.94, 0.99] | no clear difference | 1.01 [0.97, 1.02] |
| Completed-frame interval p50 (ms) | 16.6 [16.6, 16.7] | 16.7 [16.7, 16.7] | 1.01 [1.00, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op (ms) | 2.63 [2.30, 3.13] | 5.37 [4.48, 5.61] | 2.04 [1.51, 2.37] | loss | 1.05 [0.85, 1.28] |
| All-process CPU per frame/op (ms) | 4.20 [3.60, 4.92] | 7.36 [6.39, 7.65] | 1.75 [1.32, 2.05] | loss | 1.14 [0.96, 1.44] |
| GPU-process memory (MB) | 259 [258, 261] | 1220 [1217, 1223] | 4.71 [4.67, 4.75] | loss | 1.00 [0.99, 1.01] |
| Total browser memory (MB) | 431 [429, 432] | 1395 [1383, 1399] | 3.24 [3.21, 3.26] | loss | 1.01 [1.00, 1.01] |

#### magnetic_fluids

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 87.5 [76.8, 118] | 93.8 [90.8, 95.8] | 1.07 [0.79, 1.24] | no clear difference | 0.98 [0.66, 1.14] |
| Cold first job p95 (ms) | 127 [118, 127] | 126 [95.8, 126] | 0.99 [0.75, 1.06] | no clear difference | 0.78 [0.72, 0.83] |
| Frame/op completion p50 (ms) | 2.90 [2.60, 3.20] | 3.90 [3.80, 3.90] | 1.34 [1.22, 1.50] | loss | 0.93 [0.84, 1.08] |
| Frame/op completion p95 (ms) | 4.50 [4.30, 4.50] | 5.50 [5.40, 5.70] | 1.22 [1.19, 1.30] | loss | 0.96 [0.93, 1.00] |
| Completed-frame interval p50 (ms) | 16.7 [16.6, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.01] | no clear difference | 1.00 [0.99, 1.01] |
| GPU-process CPU per frame/op (ms) | 2.12 [2.03, 2.56] | 5.57 [5.42, 5.68] | 2.63 [2.17, 2.79] | loss | 1.18 [0.97, 1.26] |
| All-process CPU per frame/op (ms) | 3.16 [3.03, 3.79] | 6.80 [6.54, 6.86] | 2.15 [1.76, 2.26] | loss | 1.28 [1.06, 1.37] |
| GPU-process memory (MB) | 244 [244, 245] | 7755 [7742, 7756] | 31.73 [31.64, 31.76] | loss | 1.00 [0.95, 1.01] |
| Total browser memory (MB) | 425 [423, 426] | 7936 [7922, 7937] | 18.69 [18.62, 18.75] | loss | 0.97 [0.97, 1.01] |

#### prismatic_fluids

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 126 [97.5, 132] | 113 [107, 118] | 0.90 [0.83, 1.19] | no clear difference | 0.75 [0.71, 1.00] |
| Cold first job p95 (ms) | 139 [132, 139] | 127 [118, 127] | 0.91 [0.85, 0.93] | win | 0.81 [0.72, 0.82] |
| Frame/op completion p50 (ms) | 3.30 [3.20, 3.40] | 4.70 [4.40, 4.80] | 1.42 [1.32, 1.48] | loss | 1.06 [0.97, 1.09] |
| Frame/op completion p95 (ms) | 5.00 [4.80, 5.10] | 6.80 [6.50, 6.80] | 1.36 [1.30, 1.42] | loss | 1.02 [0.98, 1.06] |
| Completed-frame interval p50 (ms) | 16.7 [16.7, 16.7] | 16.7 [16.6, 16.7] | 1.00 [0.99, 1.00] | no clear difference | 1.00 [0.99, 1.00] |
| GPU-process CPU per frame/op (ms) | 2.57 [2.09, 2.75] | 5.63 [5.35, 5.72] | 2.19 [1.97, 2.70] | loss | 0.99 [0.91, 1.40] |
| All-process CPU per frame/op (ms) | 3.77 [3.06, 4.09] | 6.76 [6.35, 6.91] | 1.79 [1.60, 2.21] | loss | 1.07 [0.98, 1.52] |
| GPU-process memory (MB) | 237 [235, 237] | 7747 [7732, 7762] | 32.69 [32.62, 32.92] | loss | 1.00 [0.99, 1.00] |
| Total browser memory (MB) | 417 [416, 419] | 7930 [7913, 7943] | 19.04 [18.91, 19.07] | loss | 1.01 [1.00, 1.01] |

The full table, including the `fawn_dawn` arm values and frame-interval p95,
is in [macos-summary.md](macos-summary.md) and
[macos-summary.json](macos-summary.json).

### Wins

- `heavy_particles`: cold first job p95 0.74x [0.69, 0.83].
- `prismatic_fluids`: cold first job p95 0.91x [0.85, 0.93].

Both are p95 over 12 launches, and Chrome's slowest launch drives each one. The
same-binary `fawn_dawn` control is lower still (0.66x and 0.81x). Neither win
can be attributed to Doe.

### Losses

| Page | Loss (Fawn+Doe / Chrome) |
|---|---|
| all six | GPU-process CPU per frame/op 2.0-2.6x; all-process CPU 1.75-2.15x |
| all six | GPU-process memory 3.7x (compute), 4.7x (image lab), 13x (cycle), 28-33x (particles, fluids); total memory 2.5-19x |
| heavy_particles, magnetic_fluids, prismatic_fluids | submit-to-completion p50 1.34-1.42x, p95 1.22-1.36x |
| cycle | completed-frame interval p50 1.46x (about 40 fps instead of 57); completion p50 1.38x; cold first job p50 1.27x |
| compute_atomic_canvas | operation completion p50 1.05x, p95 1.07x |

In every one of these losses, `fawn_dawn` is within noise of Chrome, so they
are Doe-specific. The one partial exception is all-process CPU on
magnetic_fluids, 1.28x [1.06, 1.37] for `fawn_dawn`.

## Likely owners of the losses (code reading)

1. **GPU memory growth of about one canvas per frame.** It is linear at
   ~0.9 GB/s at 60 fps on a 2560x1426 canvas (14.6 MB per frame) and unbounded
   by end of window: 26 GB after 30 s in a separate diagnostic launch. On Metal,
   WebGPU `destroy()` frees nothing:
   - `wgpuTextureDestroy` evicts memory only on Vulkan
     (`runtime/c/native/src/textures.c:230-259`). On Metal it only sets
     `destroyed` (`runtime/c/resources/src/textures.c:126-129`), and the
     MTLTexture is freed only at the last reference
     (`runtime/c/backends/metal/src/textures.c:141-151`).
   - `wgpuBufferDestroy` likewise keeps the backend allocation until release
     (`runtime/c/native/src/buffers.c:146-158`).

   Chromium's per-frame staging texture is destroyed explicitly
   (`webgpu_decoder_impl.cc` `~SharedImageRepresentationAndAccessSkiaFallback`,
   pinned tree line 630). Its memory therefore lingers until every wire and
   forwarding reference drops. Which object is retained is **hypothesis**; the
   destroy semantics are **confirmed from code**. The next step is to count live
   Metal textures and buffers per frame.
2. **GPU-process CPU, completion latency and cycle frame drops.** On macOS,
   forced Doe presents every canvas frame through the Skia staging fallback, not
   zero-copy IOSurface. The decoder requests interop features only under
   `IS_LINUX` (`browser/chromium/patches/chromium-doe-runtime.patch:1145-1160`).
   The route admits zero-copy only for Vulkan OPAQUE_FD/SYNC_FD and otherwise
   logs `skia_staging reason=interop_not_enabled` (same patch, lines
   1306-1320). Each frame then creates a canvas texture, copies it into a
   MapRead buffer, and blocks on `MapAsync`/`WaitAny` before writing Skia
   (`webgpu_decoder_impl.cc` `UploadContentsToSkia`, pinned tree lines
   ~820-900). Doe already implements IOSurface shared-texture import
   (`runtime/c/native/src/shared_memory.c:168-205`,
   `runtime/c/backends/metal/src/shared_memory.c:5-118`), but the decoder never
   requests it. Not checked: whether Chromium's IOSurface `ProduceDawn` path
   works with Doe's import subset.
3. **Per-frame blocking inside Doe's Metal texture creation.** This compounds
   owner 2. Every Metal texture creation waits for all pending GPU work
   (`native_settle_pending`, `runtime/c/native/src/metal_textures.c:125`). It
   then submits its attachment clear with a synchronous wait
   (`runtime/c/native/src/metal_textures.c:57` →
   `native_submit_backend_commands`, `runtime/c/native/src/queue.c:427-430`).
   Under staging, both happen on every presented frame, which serializes the
   GPU process behind page work. This is **confirmed from code**; its share of
   the 2-2.6x CPU and the latency loss is **not separately measured**.
4. The 5-7% compute operation latency loss is consistent with owner 3: the
   dispatch batch and the staging texture's synchronous clear serialize on one
   queue. **Hypothesis**; no per-phase trace was taken.

## Limits

- One Mac and one display. Frames are vsync-capped at 60 Hz, so cadence differs
  only where a page misses frames (cycle).
- Memory is a single sample at the end of the window. Because Doe's growth is
  linear, its value depends on the window length (2 s warm-up plus 6 s
  measured).
- An init-script observer and an extra rAF loop ran in every arm.
- Same-milestone baseline, but a different build configuration (official PGO vs
  Fawn's non-official build); the `fawn_dawn` control covers this.
- This is diagnostic evidence, not a claim-indexed row. It does not satisfy
  GOALS acceptance item 1 or the browser promotion contracts.

## Files

- `macos-summary.json` / `macos-summary.md`: all metrics, CIs, verdicts,
  failure, provider and position counts.
- `raw-evidence-macos.tar.gz`: per-launch specs and receipts, the schedule,
  the orchestration log, rep-0 screenshots and the harness source. Paths and
  user names are replaced with placeholders.
- `SHA256SUMS`.

Reproduce, from a directory holding the harness:
`node orchestrate.mjs --config config.macos.json --out RUN`, then
`python3 -I analyze.py RUN`.
