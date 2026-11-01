# Fawn 158 vs stock Chromium 158 on WebGPU pages

Disposition: **diagnostic comparison**. On macOS (Metal) and Windows (D3D12),
Fawn with forced Doe produced correct output on every page and launch (216 of
216 launches per run). On macOS before the presentation and lifetime fixes,
forced Doe lost on GPU-process CPU, memory and frame completion latency on every
page because of a presentation-path leak of about one canvas-sized allocation per
frame, Skia staging presentation, and synchronous waits in texture creation; the
follow-up macOS run (`038fceebd` / `8bb0fb77a`) eliminated the memory and
GPU-process CPU losses. On Windows (D3D12), forced Doe currently exhibits the
same unaddressed presentation-path memory growth (2.7-31x GPU-process memory),
GPU-process CPU overhead (2.0-3.0x), and frame completion latency losses
(1.05-3.91x p50 on the five canvas-heavy pages).

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

## After the fixes (Doe 038fceebd)

The three owners above were fixed, Fawn was rebuilt incrementally from the same
Chromium 158 tree and repackaged (`fawn-doe-158.0.8108.0-macos-arm64.zip`,
sha256 `c87529233545ccaa3012f903acd3364c9249a399a9618e9a7b1017ac5a537128`;
Doe runtime sha256 `d1e165381a48a2077634cc53c101e7e07a27fe856e6cb1f66b064a3c4e20dfc5`,
compiler unchanged). Its clean-install verifier passed forced-Dawn and forced-Doe
smoke (`activeRuntimeProof.matched` on both). Then the same driver, harness,
pages, arms, balanced order and n (12 x 6 pages x 3 arms) ran again.

- **Memory** (`74545d796`, `8e076fa8e`): the leaked reference was Dawn's
  WebGPU backend never releasing inner render and compute pass encoders. A Doe
  pass retains its encoder and attachment views, so every destroyed canvas
  texture stayed alive (counted in the GPU process: per frame two encoders,
  three render passes and two compute passes; every destroyed canvas texture
  held by its handle and one view whose references included those passes).
  Doe's Metal `destroy()` now also frees the texture, its native views and buffer
  allocations once no recording owns them.
- **Presentation** (`038fceebd`, `8e076fa8e`): forced Doe on macOS imports the
  canvas IOSurface zero-copy with MTLSharedEvent fences; the Skia staging copy
  and readback are gone for packed 8-bit canvases.
- **Texture creation** (`74545d796`): no settle of pending work and no waited
  clear; zero initialization is a clear recorded in the first using command.

Totals: 216 of 216 launches completed with no failures, no device loss and no
uncaptured errors; every launch drained. `libwebgpu_doe_c.dylib` was mapped in
all 72 `fawn_doe` launches and in none of the 144 others. The compute oracle
matched in 36 of 36 launches, and `compute_atomic_canvas` and `image_lab`
screenshot hashes were identical across all launches and arms.

Fawn+Doe before and after, each against Chrome 158 in its own run (the
"Chrome 158" column is the after run). Verdicts use the same rule as above.

#### compute_atomic_canvas

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 94.3 [87.2, 96.9] | 112 | 1.08 [0.903, 1.16] | 102 [97.3, 107] | 1.09 [1.02, 1.21] | loss | 1.03 [0.989, 1.19] |
| Frame/op completion p50 (ms) | 35.2 [34.9, 35.5] | 36.5 | 1.05 [1.04, 1.09] | 36.1 [36, 37] | 1.03 [1.02, 1.05] | no clear difference | 1.00 [0.989, 1.02] |
| Frame/op completion p95 (ms) | 36.1 [35.6, 36.3] | 38.6 | 1.07 [1.04, 1.09] | 37.4 [37, 38] | 1.04 [1.02, 1.06] | loss | 1.01 [0.994, 1.02] |
| Completed-frame interval p50 (ms) | 49.8 [49.7, 49.9] | 49.8 | 1.00 [1, 1.01] | 49.8 [49.6, 49.9] | 1.00 [0.996, 1] | no clear difference | 1.00 [0.996, 1] |
| GPU-process CPU per frame/op (ms) | 5.48 [5.12, 5.61] | 7.69 | 2.25 [1.76, 2.66] | 5.1 [4.71, 5.26] | 0.93 [0.846, 1.02] | no clear difference | 1.15 [0.994, 1.24] |
| All-process CPU per frame/op (ms) | 8.6 [8.11, 8.71] | 11.3 | 2.01 [1.52, 2.38] | 9.76 [8.65, 10.4] | 1.13 [0.999, 1.25] | no clear difference | 1.22 [1.1, 1.32] |
| GPU-process memory (MB) | 197 [193, 198] | 724 | 3.72 [3.63, 3.74] | 192 [190, 195] | 0.97 [0.96, 0.994] | no clear difference | 1.00 [0.99, 1.02] |
| Total browser memory (MB) | 361 [348, 365] | 890 | 2.47 [2.44, 2.49] | 360 [356, 364] | 1.00 [0.979, 1.04] | no clear difference | 1.01 [0.997, 1.06] |

#### cycle

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 173 [169, 196] | 233 | 1.27 [1.1, 1.36] | 229 [225, 294] | 1.32 [1.16, 1.71] | loss | 1.13 [0.971, 1.3] |
| Frame/op completion p50 (ms) | 12.5 [11.8, 13.4] | 17.5 | 1.38 [1.18, 1.44] | 14.7 [13.9, 14.9] | 1.18 [1.06, 1.26] | loss | 1.05 [0.836, 1.13] |
| Frame/op completion p95 (ms) | 21.3 [20.6, 21.6] | 21.9 | 1.10 [1.03, 1.37] | 22.1 [21.8, 22.6] | 1.04 [1.01, 1.08] | loss | 0.99 [0.925, 1.02] |
| Completed-frame interval p50 (ms) | 18.5 [17.9, 19.4] | 25.7 | 1.46 [1.13, 1.74] | 20.1 [18.7, 21] | 1.09 [1, 1.15] | no clear difference | 1.01 [0.928, 1.07] |
| GPU-process CPU per frame/op (ms) | 5.56 [4.74, 5.77] | 6.5 | 2.01 [1.66, 2.38] | 5.95 [5.44, 6.21] | 1.07 [0.96, 1.29] | no clear difference | 1.11 [0.889, 1.38] |
| All-process CPU per frame/op (ms) | 8.23 [7.01, 8.8] | 8.94 | 1.81 [1.51, 2.12] | 10.3 [9.46, 10.9] | 1.25 [1.11, 1.47] | loss | 1.22 [0.956, 1.48] |
| GPU-process memory (MB) | 432 [432, 433] | 5701 | 13.26 [13.2, 13.8] | 441 [434, 441] | 1.02 [1, 1.02] | no clear difference | 1.00 [0.998, 1] |
| Total browser memory (MB) | 637 [635, 638] | 5901 | 9.36 [9.27, 9.73] | 644 [617, 645] | 1.01 [0.969, 1.02] | no clear difference | 1.00 [0.996, 1] |

#### heavy_particles

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 63.9 [59.1, 70.5] | 79 | 0.98 [0.77, 1.32] | 83 [80.4, 90.6] | 1.30 [1.17, 1.46] | loss | 1.18 [0.99, 1.29] |
| Frame/op completion p50 (ms) | 4 [3.8, 4.1] | 5.5 | 1.38 [1.34, 1.43] | 4.9 [4.4, 5.1] | 1.23 [1.1, 1.31] | loss | 1.03 [0.976, 1.08] |
| Frame/op completion p95 (ms) | 5.3 [4.6, 5.4] | 6.5 | 1.27 [1.23, 1.4] | 5.7 [4.9, 6] | 1.08 [0.925, 1.26] | no clear difference | 0.98 [0.852, 1.15] |
| Completed-frame interval p50 (ms) | 16.7 [16.7, 16.7] | 16.7 | 1.00 [0.994, 1] | 16.7 [16.7, 16.7] | 1.00 [1, 1] | no clear difference | 1.00 [1, 1] |
| GPU-process CPU per frame/op (ms) | 3.87 [2.96, 3.92] | 5.52 | 2.43 [2.08, 2.7] | 4.1 [3.47, 4.28] | 1.06 [0.892, 1.39] | no clear difference | 1.06 [0.832, 1.39] |
| All-process CPU per frame/op (ms) | 5.6 [4.2, 5.73] | 6.63 | 1.99 [1.73, 2.28] | 6.58 [5.33, 6.65] | 1.18 [0.952, 1.57] | no clear difference | 1.16 [0.88, 1.54] |
| GPU-process memory (MB) | 276 [276, 278] | 7788 | 28.24 [28.2, 28.3] | 269 [269, 270] | 0.98 [0.969, 0.977] | no clear difference | 1.00 [0.995, 1] |
| Total browser memory (MB) | 458 [458, 459] | 7968 | 17.49 [17.4, 17.5] | 454 [451, 455] | 0.99 [0.984, 0.993] | no clear difference | 1.00 [0.965, 1.01] |

#### image_lab

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 56.5 [45.5, 63] | 71.6 | 1.27 [0.825, 1.38] | 76.6 [68.7, 80.4] | 1.36 [1.15, 1.68] | loss | 1.26 [1, 1.57] |
| Frame/op completion p50 (ms) | 16.5 [16.4, 16.6] | 16.6 | 1.00 [1, 1.01] | 16.5 [16.4, 16.5] | 1.00 [0.994, 1.01] | no clear difference | 1.00 [0.988, 1.01] |
| Frame/op completion p95 (ms) | 18.5 [18.3, 18.6] | 18 | 0.97 [0.937, 0.989] | 18.5 [18.5, 18.7] | 1.00 [0.995, 1.01] | no clear difference | 1.02 [1, 1.03] |
| Completed-frame interval p50 (ms) | 16.6 [16.6, 16.7] | 16.7 | 1.01 [1, 1.01] | 16.6 [16.6, 16.7] | 1.00 [0.994, 1.01] | no clear difference | 1.01 [0.994, 1.01] |
| GPU-process CPU per frame/op (ms) | 4.42 [3.37, 5.03] | 5.37 | 2.04 [1.51, 2.37] | 4.56 [4, 5.06] | 1.03 [0.834, 1.46] | no clear difference | 1.17 [0.952, 1.57] |
| All-process CPU per frame/op (ms) | 6.82 [5.24, 7.67] | 7.36 | 1.75 [1.32, 2.05] | 8.42 [6.87, 9.15] | 1.24 [0.913, 1.72] | no clear difference | 1.28 [1.05, 1.7] |
| GPU-process memory (MB) | 261 [260, 261] | 1220 | 4.71 [4.67, 4.75] | 252 [251, 254] | 0.97 [0.962, 0.974] | win | 1.01 [0.999, 1.02] |
| Total browser memory (MB) | 435 [434, 437] | 1395 | 3.24 [3.21, 3.26] | 429 [426, 431] | 0.99 [0.977, 0.993] | no clear difference | 1.01 [0.983, 1.02] |

#### magnetic_fluids

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 80.2 [77.4, 87.5] | 93.8 | 1.07 [0.792, 1.24] | 108 [89.6, 120] | 1.35 [1.08, 1.51] | loss | 1.13 [0.977, 1.29] |
| Frame/op completion p50 (ms) | 3.7 [3, 3.8] | 3.9 | 1.34 [1.22, 1.5] | 3.9 [3.1, 4.1] | 1.05 [0.838, 1.37] | no clear difference | 1.03 [0.946, 1.3] |
| Frame/op completion p95 (ms) | 4.8 [4.4, 5.1] | 5.5 | 1.22 [1.19, 1.3] | 5.4 [4.7, 5.6] | 1.12 [0.94, 1.25] | no clear difference | 1.06 [0.902, 1.18] |
| Completed-frame interval p50 (ms) | 16.7 [16.6, 16.7] | 16.7 | 1.00 [0.994, 1.01] | 16.7 [16.6, 16.7] | 1.00 [0.994, 1.01] | no clear difference | 1.00 [0.994, 1.01] |
| GPU-process CPU per frame/op (ms) | 3.33 [2.73, 3.83] | 5.57 | 2.63 [2.17, 2.79] | 3.88 [3.1, 4.09] | 1.16 [0.835, 1.44] | no clear difference | 1.25 [0.978, 1.53] |
| All-process CPU per frame/op (ms) | 5.01 [3.93, 5.67] | 6.8 | 2.15 [1.76, 2.26] | 6.31 [4.92, 6.6] | 1.26 [0.923, 1.61] | no clear difference | 1.31 [1.06, 1.67] |
| GPU-process memory (MB) | 247 [235, 247] | 7755 | 31.73 [31.6, 31.8] | 183 [183, 186] | 0.74 [0.741, 0.787] | win | 0.99 [0.944, 1.04] |
| Total browser memory (MB) | 427 [417, 430] | 7936 | 18.69 [18.6, 18.8] | 367 [366, 372] | 0.86 [0.852, 0.883] | win | 1.00 [0.971, 1.03] |

#### prismatic_fluids

| Metric | Chrome 158 (after run) | Fawn+Doe before | before / Chrome [95% CI] | Fawn+Doe after | after / Chrome [95% CI] | after verdict | Fawn+Dawn after / Chrome [95% CI] |
|---|---|---|---|---|---|---|---|
| Cold first job p50 (ms) | 89.5 [84.1, 94] | 113 | 0.90 [0.827, 1.19] | 122 [115, 128] | 1.36 [1.23, 1.51] | loss | 1.20 [1.11, 1.35] |
| Frame/op completion p50 (ms) | 3.6 [3.4, 3.7] | 4.7 | 1.42 [1.32, 1.48] | 4.2 [3.8, 4.4] | 1.17 [1.05, 1.26] | loss | 1.06 [0.914, 1.12] |
| Frame/op completion p95 (ms) | 5.3 [5.2, 5.4] | 6.8 | 1.36 [1.3, 1.42] | 6.5 [5.4, 6.6] | 1.23 [1.02, 1.25] | loss | 0.98 [0.963, 1.06] |
| Completed-frame interval p50 (ms) | 16.7 [16.7, 16.7] | 16.7 | 1.00 [0.994, 1] | 16.7 [16.6, 16.7] | 1.00 [0.994, 1] | no clear difference | 1.00 [0.994, 1] |
| GPU-process CPU per frame/op (ms) | 3.91 [2.93, 3.94] | 5.63 | 2.19 [1.97, 2.7] | 4.2 [3.35, 4.27] | 1.07 [0.854, 1.45] | no clear difference | 1.11 [0.813, 1.51] |
| All-process CPU per frame/op (ms) | 5.62 [4.22, 5.76] | 6.76 | 1.79 [1.6, 2.21] | 6.62 [5.22, 6.84] | 1.18 [0.929, 1.58] | no clear difference | 1.22 [0.87, 1.62] |
| GPU-process memory (MB) | 238 [237, 240] | 7747 | 32.69 [32.6, 32.9] | 190 [190, 190] | 0.80 [0.79, 0.8] | win | 1.00 [0.989, 1] |
| Total browser memory (MB) | 422 [403, 424] | 7930 | 19.04 [18.9, 19.1] | 377 [375, 377] | 0.89 [0.887, 0.934] | win | 1.00 [0.997, 1.05] |

Reading the table:

- GPU-process and total memory now match Chrome or are lower on every page
  (was 3.7-33x and 2.5-19x).
- GPU-process CPU per frame/op is no longer a loss on any page (was 2.0-2.6x;
  now 0.93-1.16x, every CI including 1.0). All-process CPU remains a loss on
  `cycle` (1.25x); `fawn_dawn` shows a similar 1.22x [0.96, 1.48] there.
- `cycle` completed-frame interval is no longer a loss (1.46x to 1.09x).
  Submit-to-completion p50 losses shrank (1.05-1.42x to 1.03-1.23x) but remain
  on `cycle`, `heavy_particles` and `prismatic_fluids`; p95 losses remain on
  `compute_atomic_canvas` (1.04x), `cycle` (1.04x) and `prismatic_fluids` (1.23x).
- Cold first job is not improved. Its ratios look worse partly because Chrome's
  cold start was faster and less bimodal in this run, and because the first
  launch (r00) of the freshly extracted Fawn app was slow in both Fawn arms
  (`fawn_doe` 293-4434 ms, `fawn_dawn` 281-519 ms on the first three pages),
  which dominates the p95 rows in `macos-after-summary.md`.
- Absolute CPU values differ between the two runs for every arm, Chrome
  included (host load from the shared timing job differed), so only the
  within-run ratios are comparable.

## Windows (D3D12) results (Fawn+Doe vs Chrome 158)

### What was compared on Windows

| Arm | Browser | Runtime selection |
|---|---|---|
| `chrome` | Chrome for Testing 158.0.8108.0 (`chrome-win64/chrome.exe`, official PGO build) | stock Dawn |
| `fawn_doe` | Fawn 158.0.8108.0 diagnostic package (`chrome.exe`) | `--use-webgpu-runtime=doe` plus `--doe-webgpu-library-path=.../lib/doe/webgpu_doe_c.dll` |
| `fawn_dawn` | same Fawn binary | `--use-webgpu-runtime=dawn` (same-binary control) |

- Host: win-host, AMD Ryzen 7 6800H with Radeon Graphics (8 cores / 16 logical
  processors, 30 GiB visible RAM), integrated AMD Radeon Graphics (`amd / rdna-2`,
  driver `32.0.21030.2001`), Windows 11 Pro 10.0.26300 (Build 26300), 2560x1440
  120 Hz display, device pixel ratio 1.2 (1268x708 viewport, 1521x849 canvas).
- Fawn diagnostic archive `Fawn-Doe-windows-x64-158.0.8108.0-doe.8b5637d28.zip`,
  sha256 `16bb33813370b297fc38364a733ff5019274b5b0b3daf505d290485061cc96c6`.
  Built from Doe `6258de498` (pre-rewrite stamp `8b5637d28`), Chromium
  `4a6fb38567da` (`158.0.8108.0`), Dawn `758e5e6a974e`. Sandboxed forced-Dawn
  smoke (`a286d14f…`), forced-Doe smoke (`8f56b9c1…`), provider identity probe
  (`426e7147…`), start-page forced-Doe smoke (`8920218a…`) and launcher controls
  (`cd56add8…`) passed before the benchmark run.
- Doe runtime `lib/doe/webgpu_doe_c.dll` sha256
  `09c4dbf9cb5ebad5a27d612faa7dae16eb8d0e072d5fcf334ea595a1badb5a09`, compiler
  `lib/doe/doe_compiler.dll` sha256
  `288daf201a6a1cd20f21991b0d9f18b286160a8b0203f73080805e89d07ea45d`. Fawn
  `Fawn.exe` sha256 `66aaab0325d8…`, `chrome.exe` sha256 `38503e854bce…`,
  `158.0.8108.0/chrome.dll` sha256 `cfa797987e9e…`. Chrome for Testing
  `chrome.exe` sha256 `da235cbc50f6…`.
- Pages: `browser/chromium/resources/` served unchanged from Doe `6258de498`.
  Per-request sha256 values are recorded in each launch receipt.

### Windows method notes

- Same design as macOS: 12 repetitions x 6 pages x 3 arms = 216 fresh headed
  launches with fresh profiles, sandbox enabled, and balanced 6-permutation arm
  orderings, serialized under a host GPU lock file with no compiler or parallel
  GPU job active.
- **Memory** on Windows is sampled at the end of the measured window via
  `Get-Process` and Windows `\GPU Process Memory(pid_*)\*` performance counters:
  - GPU-process memory = GPU process `PrivateMemorySize64`;
  - GPU-process graphics memory = `Dedicated Usage + Shared Usage` from
    `\GPU Process Memory(pid_<gpu>)\*`;
  - Total browser memory = sum of `PrivateMemorySize64` across the browser's
    process tree.
- **Provider**: `Get-Process -Module` on the GPU process confirmed that
  `webgpu_doe_c.dll` and `doe_compiler.dll` (along with `d3d12.dll`,
  `D3D12Core.dll`, `dxcompiler.dll` and `dxil.dll`) were mapped in all 72
  `fawn_doe` launches and in 0 of the 144 `chrome` and `fawn_dawn` launches.
  The adapter was `amd / rdna-2` in all 216 launches.
- **Correctness**: 216 of 216 launches succeeded with no fallback, no device
  loss and no uncaptured errors; every launch drained (`gpuCompletion`
  completed). `compute_atomic_canvas` matched the exact `sha256_exact_v1` oracle
  in 36 of 36 launches (`12/12` per arm), and both deterministic pages
  (`compute_atomic_canvas` and `image_lab`) produced 1 distinct screenshot RGBA
  hash identical across all launches and all three arms.

#### compute_atomic_canvas (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 928 [902, 947] | 1011 [1002, 1032] | 1.09 [1.06, 1.12] | loss | 0.99 [0.96, 1.01] |
| Cold first job p95 (ms) | 967 [947, 967] | 1082 [1032, 1082] | 1.12 [1.07, 1.13] | loss | 1.03 [0.96, 1.04] |
| Frame/op completion p50 (ms) | 517 [517, 518] | 508 [508, 508] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| Frame/op completion p95 (ms) | 520 [520, 520] | 510 [509, 511] | 0.98 [0.98, 0.98] | no clear difference | 1.00 [1.00, 1.00] |
| Completed-frame interval p50 (ms) | 520 [520, 521] | 515 [515, 515] | 0.99 [0.99, 0.99] | no clear difference | 1.00 [1.00, 1.00] |
| GPU-process CPU per frame/op (ms) | 10.1 [9.88, 10.5] | 20.5 [20.0, 20.8] | 2.02 [1.91, 2.09] | loss | 1.53 [1.44, 1.62] |
| All-process CPU per frame/op (ms) | 21.7 [21.1, 22.2] | 30.7 [30.0, 33.3] | 1.42 [1.36, 1.55] | loss | 1.69 [1.64, 1.78] |
| GPU-process memory (MB) | 152 [151, 152] | 409 [409, 410] | 2.70 [2.69, 2.71] | loss | 1.01 [1.00, 1.01] |
| GPU-process graphics memory (MB) | 85.8 [85.8, 86.1] | 274 [274, 274] | 3.19 [3.18, 3.20] | loss | 1.00 [1.00, 1.00] |
| Total browser memory (MB) | 315 [313, 317] | 572 [570, 574] | 1.81 [1.80, 1.83] | loss | 1.00 [1.00, 1.02] |

#### cycle (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 1079 [1018, 1148] | 1564 [1552, 1596] | 1.45 [1.35, 1.54] | loss | 0.99 [0.92, 1.05] |
| Cold first job p95 (ms) | 1513 [1148, 1513] | 1658 [1596, 1658] | 1.10 [1.09, 1.44] | loss | 0.73 [0.72, 0.97] |
| Frame/op completion p50 (ms) | 14.0 [13.0, 14.7] | 17.0 [16.9, 17.1] | 1.21 [1.15, 1.31] | loss | 1.07 [1.02, 1.15] |
| Frame/op completion p95 (ms) | 18.0 [17.9, 18.1] | 19.3 [19.2, 19.5] | 1.07 [1.06, 1.08] | loss | 1.01 [1.00, 1.02] |
| Completed-frame interval p50 (ms) | 19.1 [19.1, 19.2] | 23.3 [23.2, 23.4] | 1.22 [1.21, 1.23] | loss | 1.01 [1.00, 1.02] |
| GPU-process CPU per frame/op (ms) | 2.81 [2.78, 2.93] | 6.55 [6.47, 7.01] | 2.33 [2.23, 2.50] | loss | 1.29 [1.24, 1.31] |
| All-process CPU per frame/op (ms) | 5.43 [5.13, 5.95] | 12.3 [12.1, 13.0] | 2.26 [2.05, 2.50] | loss | 1.35 [1.22, 1.46] |
| GPU-process memory (MB) | 289 [284, 290] | 3073 [3047, 3080] | 10.63 [10.52, 10.80] | loss | 1.00 [1.00, 1.02] |
| GPU-process graphics memory (MB) | 191 [188, 191] | 3151 [3125, 3170] | 16.51 [16.38, 16.72] | loss | 1.00 [1.00, 1.01] |
| Total browser memory (MB) | 487 [483, 490] | 3268 [3246, 3277] | 6.70 [6.64, 6.76] | loss | 1.00 [0.99, 1.01] |

#### heavy_particles (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 582 [485, 608] | 669 [654, 728] | 1.15 [1.08, 1.41] | loss | 0.89 [0.85, 1.08] |
| Cold first job p95 (ms) | 960 [608, 960] | 761 [728, 761] | 0.79 [0.78, 1.24] | no clear difference | 0.68 [0.58, 1.02] |
| Frame/op completion p50 (ms) | 3.50 [3.40, 3.70] | 13.7 [13.0, 14.3] | 3.91 [3.59, 4.09] | loss | 1.09 [1.03, 1.12] |
| Frame/op completion p95 (ms) | 4.10 [4.10, 4.10] | 16.1 [16.0, 16.9] | 3.93 [3.90, 4.12] | loss | 1.00 [0.98, 1.00] |
| Completed-frame interval p50 (ms) | 9.60 [9.50, 9.70] | 10.7 [10.4, 10.9] | 1.11 [1.08, 1.14] | loss | 0.98 [0.96, 0.99] |
| GPU-process CPU per frame/op (ms) | 1.73 [1.72, 1.75] | 4.90 [4.83, 5.05] | 2.84 [2.79, 2.93] | loss | 1.26 [1.25, 1.32] |
| All-process CPU per frame/op (ms) | 2.55 [2.52, 3.17] | 6.57 [6.47, 7.37] | 2.57 [2.06, 2.90] | loss | 1.43 [1.14, 1.67] |
| GPU-process memory (MB) | 206 [205, 213] | 5581 [5377, 5629] | 27.07 [25.49, 27.39] | loss | 1.04 [1.00, 1.05] |
| GPU-process graphics memory (MB) | 121 [121, 121] | 5999 [5815, 6070] | 49.65 [48.12, 50.23] | loss | 1.00 [1.00, 1.00] |
| Total browser memory (MB) | 380 [377, 385] | 5753 [5545, 5800] | 15.12 [14.52, 15.34] | loss | 1.01 [1.00, 1.03] |

#### image_lab (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 576 [567, 595] | 709 [699, 712] | 1.23 [1.18, 1.25] | loss | 0.99 [0.96, 1.02] |
| Cold first job p95 (ms) | 957 [595, 957] | 747 [712, 747] | 0.78 [0.75, 1.24] | no clear difference | 0.62 [0.61, 1.00] |
| Frame/op completion p50 (ms) | 9.10 [9.10, 9.20] | 9.60 [9.50, 9.60] | 1.05 [1.04, 1.05] | loss | 1.00 [0.99, 1.00] |
| Frame/op completion p95 (ms) | 10.9 [10.9, 11.1] | 11.1 [10.9, 11.1] | 1.02 [0.99, 1.02] | no clear difference | 0.97 [0.91, 1.01] |
| Completed-frame interval p50 (ms) | 9.20 [9.20, 9.20] | 9.80 [9.70, 9.80] | 1.07 [1.05, 1.07] | loss | 1.00 [1.00, 1.00] |
| GPU-process CPU per frame/op (ms) | 2.36 [2.18, 2.43] | 4.76 [4.72, 4.76] | 2.02 [1.95, 2.18] | loss | 1.13 [1.09, 1.24] |
| All-process CPU per frame/op (ms) | 3.89 [3.68, 4.16] | 7.45 [7.40, 7.47] | 1.92 [1.79, 2.03] | loss | 1.39 [1.29, 1.48] |
| GPU-process memory (MB) | 224 [222, 226] | 856 [803, 859] | 3.82 [3.58, 3.87] | loss | 1.00 [0.99, 1.01] |
| GPU-process graphics memory (MB) | 121 [119, 121] | 496 [494, 496] | 4.10 [4.08, 4.16] | loss | 1.00 [0.99, 1.02] |
| Total browser memory (MB) | 386 [385, 390] | 1020 [967, 1024] | 2.64 [2.48, 2.66] | loss | 1.00 [0.99, 1.01] |

#### magnetic_fluids (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 606 [578, 961] | 716 [709, 740] | 1.18 [0.74, 1.25] | no clear difference | 0.93 [0.58, 0.98] |
| Cold first job p95 (ms) | 1005 [961, 1005] | 786 [740, 786] | 0.78 [0.74, 0.82] | win | 0.62 [0.57, 0.65] |
| Frame/op completion p50 (ms) | 3.70 [3.60, 3.80] | 6.80 [5.20, 9.70] | 1.84 [1.41, 2.69] | loss | 1.00 [0.97, 1.06] |
| Frame/op completion p95 (ms) | 4.00 [4.00, 4.00] | 14.6 [14.3, 14.6] | 3.65 [3.57, 3.65] | loss | 1.00 [1.00, 1.00] |
| Completed-frame interval p50 (ms) | 9.60 [9.60, 10.0] | 10.3 [10.1, 10.4] | 1.07 [1.03, 1.08] | loss | 0.98 [0.94, 1.02] |
| GPU-process CPU per frame/op (ms) | 1.57 [1.56, 1.63] | 4.76 [4.67, 4.81] | 3.02 [2.90, 3.08] | loss | 1.36 [1.30, 1.37] |
| All-process CPU per frame/op (ms) | 2.37 [2.34, 2.45] | 6.42 [6.34, 6.46] | 2.71 [2.60, 2.75] | loss | 1.49 [1.44, 1.53] |
| GPU-process memory (MB) | 185 [183, 187] | 5749 [5577, 5851] | 31.04 [29.94, 31.79] | loss | 1.02 [1.01, 1.04] |
| GPU-process graphics memory (MB) | 100 [99.9, 101] | 6184 [6108, 6243] | 61.63 [60.73, 62.44] | loss | 1.01 [0.99, 1.02] |
| Total browser memory (MB) | 357 [349, 361] | 5919 [5749, 6020] | 16.60 [16.00, 17.08] | loss | 1.02 [1.00, 1.05] |

#### prismatic_fluids (Windows D3D12)

| Metric | Chrome 158 | Fawn+Doe | Fawn+Doe / Chrome [95% CI] | Verdict | Fawn+Dawn / Chrome [95% CI] |
|---|---|---|---|---|---|
| Cold first job p50 (ms) | 646 [627, 1046] | 820 [812, 824] | 1.27 [0.78, 1.31] | no clear difference | 1.00 [0.61, 1.04] |
| Cold first job p95 (ms) | 1396 [1046, 1396] | 843 [824, 843] | 0.60 [0.59, 0.80] | win | 0.48 [0.47, 0.63] |
| Frame/op completion p50 (ms) | 3.90 [3.90, 3.90] | 6.40 [5.90, 6.70] | 1.64 [1.51, 1.72] | loss | 1.00 [1.00, 1.00] |
| Frame/op completion p95 (ms) | 5.90 [5.00, 5.90] | 10.3 [9.90, 10.4] | 1.75 [1.68, 2.06] | loss | 0.85 [0.83, 1.00] |
| Completed-frame interval p50 (ms) | 9.40 [9.30, 9.50] | 10.3 [10.2, 10.3] | 1.10 [1.08, 1.11] | loss | 0.98 [0.97, 0.99] |
| GPU-process CPU per frame/op (ms) | 1.70 [1.67, 1.71] | 5.06 [5.03, 5.08] | 2.97 [2.94, 3.04] | loss | 1.35 [1.33, 1.39] |
| All-process CPU per frame/op (ms) | 2.50 [2.45, 2.52] | 6.82 [6.79, 6.98] | 2.73 [2.70, 2.84] | loss | 1.48 [1.47, 1.52] |
| GPU-process memory (MB) | 194 [192, 197] | 5472 [5425, 5571] | 28.15 [27.67, 28.88] | loss | 1.02 [1.01, 1.04] |
| GPU-process graphics memory (MB) | 106 [106, 108] | 6012 [5967, 6069] | 56.68 [55.31, 57.21] | loss | 1.02 [1.00, 1.02] |
| Total browser memory (MB) | 370 [368, 374] | 5647 [5600, 5753] | 15.25 [15.08, 15.58] | loss | 1.01 [0.99, 1.02] |

The full Windows table, including `fawn_dawn` arm values, `launch -> first
completed job`, p99 completion latencies, and completed-frame interval p95/p99,
is in [windows-summary.md](windows-summary.md) and
[windows-summary.json](windows-summary.json).

### Windows wins

- `image_lab`: frame/op completion p99 p50 0.90x [0.88, 0.97] (11.3 ms vs
  Chrome 12.5 ms; `fawn_dawn` is 1.01x [0.95, 1.13]).
- `magnetic_fluids`: cold first job p95 0.78x [0.74, 0.82].
- `prismatic_fluids`: cold first job p95 0.60x [0.59, 0.80].

As on macOS, both cold-first-job p95 wins come from Chrome's bimodal cold-start
tail across 12 launches, and the same-binary `fawn_dawn` control is lower still
(0.62x [0.57, 0.65] and 0.48x [0.47, 0.63]). Neither cold-start win is
attributable to Doe.

### Windows losses

| Page | Loss (Fawn+Doe / Chrome) |
|---|---|
| all six | GPU-process memory 2.70x (`compute_atomic_canvas`), 3.82x (`image_lab`), 10.63x (`cycle`), 27.07-31.04x (`heavy_particles`, `prismatic_fluids`, `magnetic_fluids`); GPU-process graphics memory 3.19-61.63x; total browser memory 1.81-16.60x |
| all six | GPU-process CPU per frame/op 2.02-3.02x; all-process CPU per frame/op 1.42-2.73x |
| `heavy_particles`, `magnetic_fluids`, `prismatic_fluids` | frame/op completion p50 1.64-3.91x, p95 1.75-3.93x, p99 3.33-7.72x; completed-frame interval p50 1.07-1.11x, p99 2.31-2.57x |
| `cycle` | frame/op completion p50 1.21x, p95 1.07x, p99 1.06x; completed-frame interval p50 1.22x, p95 1.18x, p99 1.11x; cold first job p50 1.45x, p95 1.10x |
| `image_lab` | cold first job p50 1.23x; frame/op completion p50 1.05x; completed-frame interval p50 1.07x |
| `compute_atomic_canvas`, `heavy_particles` | cold first job p50 1.09x (`compute_atomic_canvas`, p95 1.12x) and 1.15x (`heavy_particles`) |

In the memory and frame/op completion losses, `fawn_dawn` is within noise of
Chrome (1.00-1.04x GPU-process memory and 0.85-1.09x frame completion p50/p95),
so those losses are Doe-specific. On CPU per frame/op, `fawn_dawn` is 1.13-1.53x
on GPU-process CPU and 1.35-1.69x on all-process CPU (reflecting Fawn's
`is_official_build=false`, `chrome_pgo_phase=0` build against official PGO
Chrome), while `fawn_doe` adds another 1.6-2.2x of GPU-process CPU on top of
`fawn_dawn` on the five canvas-heavy pages.

### Likely owners of the Windows (D3D12) losses (code reading)

1. **GPU memory growth of about one canvas per presented frame.** Over the 2 s
   warm-up + 6 s measured window at ~90-100 fps on the 1521x849 canvas,
   `fawn_doe` accumulates ~5.5-6.0 GB of GPU-process memory on
   `heavy_particles`, `magnetic_fluids` and `prismatic_fluids`, ~3.1 GB on
   `cycle`, ~856 MB on `image_lab` (60 operations) and ~409 MB on
   `compute_atomic_canvas` (30 operations). Two code-confirmed mechanisms keep
   per-frame allocations alive:
   - In the incremental Windows Chromium checkout that produced
     `158.0.8108.0/chrome.dll`, Dawn's WebGPU backend (`third_party/dawn/src/dawn/native/webgpu/CommandBufferWGPU.cpp:116-120, 261-265`)
     was compiled before `528ad37fd` added `wgpu.computePassEncoderRelease` and
     `wgpu.renderPassEncoderRelease` (`browser/chromium/patches/dawn-doe-runtime.patch:971-985`).
     Every inner render and compute pass encoder therefore stays alive, and each
     Doe render pass retains its command encoder and every attachment view
     (`runtime/c/resources/src/passes.c:69-83, 189`), which in turn retains the
     per-frame staging canvas texture.
   - Even when `texture_.Destroy()` is called at the end of each frame's Skia
     fallback access (`gpu/command_buffer/service/webgpu_decoder_impl.cc:613`),
     Doe's `wgpuTextureDestroy` (`runtime/c/native/src/textures.c:230-267`) and
     `wgpuBufferDestroy` (`runtime/c/native/src/buffers.c:146-164`) only evict
     backing memory on Vulkan (`textures.c:245-258`) and Metal
     (`textures.c:258-265`, `buffers.c:159-162`). Neither function has a
     `WGPUBackendType_D3D12` branch, so D3D12 `destroy()` only sets the frontend
     `destroyed` flag (`textures.c:236`) and keeps the `ID3D12Resource`
     (`runtime/c/backends/d3d12/src/windows_textures.c:17-22`,
     `runtime/c/native/src/d3d12_buffers.c:158-171`) until the last handle and
     view reference drops.
2. **Skia staging fallback on every presented canvas frame (no D3D12 DXGI
   shared-handle interop).** `WebGPUDecoderImpl::RequestDeviceImpl`
   (`browser/chromium/patches/chromium-doe-runtime.patch:1353-1368`) and
   `WebGPUDecoderImpl::AssociateMailboxForDoe`
   (`browser/chromium/patches/chromium-doe-runtime.patch:1503-1551`) only enable
   zero-copy shared-texture import under `IS_APPLE` (`lines 1515-1530`) and
   `IS_LINUX` (`lines 1353-1368, 1531-1535`). On `IS_WIN`, `AssociateMailboxForDoe`
   falls through to `AssociateMailboxUsingSkiaFallback` (`lines 1547-1550`), and
   Doe's C runtime (`runtime/c/native/src/shared_memory.c:99-105, 170-176`)
   accepts only Vulkan and Metal in `wgpuDeviceImportSharedFence` and
   `wgpuDeviceImportSharedTextureMemory` (no `SharedTextureMemoryDXGISharedHandle`
   or `SharedFenceDXGISharedHandle`). Every presented canvas frame therefore
   executes `UploadContentsToSkia` (`gpu/command_buffer/service/webgpu_decoder_impl.cc:789-907`),
   which allocates a fresh canvas-sized `CopyDst | MapRead` buffer (`line 806`,
   zeroed synchronously on the CPU via `ID3D12Resource_Map` + `memset` in
   `runtime/c/native/src/d3d12_buffers.c:126-128` →
   `runtime/c/backends/d3d12/src/windows_resources.c:203-221`), submits
   `CopyTextureToBuffer` (`lines 830-836`), blocks synchronously on
   `buffer.MapAsync` + `instance_.WaitAny` (`lines 840-851`), and copies the
   mapped pixels into Skia (`surface->writePixels`, `line 880`).
3. **Per-frame synchronous GPU waits inside D3D12 texture creation.** Under the
   Skia fallback above, `AssociateMailboxUsingSkiaFallback` creates a fresh
   canvas `WGPUTexture` on every frame. `native_d3d12_create_texture`
   (`runtime/c/native/src/d3d12_textures.c:24-84`) first calls
   `native_settle_pending(device)` (`line 55`), waiting synchronously for all
   in-flight GPU work to finish before calling `CreateCommittedResource`, and
   then calls `initialize(device, payload->d3d12)` (`line 65`, defined at
   `d3d12_textures.c:8-20`), which records `doe_dx_commands_initialize_texture`
   (`runtime/c/backends/d3d12/src/windows_texture_initialize.c:32-101`) and
   submits it via `native_submit_backend_commands` (`d3d12_textures.c:17` →
   `runtime/c/native/src/queue.c:423-427`), waiting synchronously on the fence.
   This is the exact D3D12 counterpart to the Metal texture-creation stall that
   was removed for macOS in `c1231428e`.
4. **Per-dispatch and per-draw D3D12 descriptor rebuilds, heap allocations, and
   resource barriers.** On `compute_atomic_canvas`, each operation submits 100
   compute dispatches before presenting once, and `fawn_doe` still uses 2.02x
   GPU-process CPU (20.5 ms vs Chrome 10.1 ms and `fawn_dawn` 15.5 ms). In the
   D3D12 backend:
   - `write_set` and `doe_dx_prepare_sets` (`runtime/c/backends/d3d12/src/windows_bindings.c:5-66, 338-347`)
     rebuild CBV/SRV/UAV/Sampler descriptors on the CPU for every draw or
     dispatch and call `ID3D12Device_CreateDescriptorHeap` whenever a sampler is
     bound (`line 343`); `doe_dx_prepare_color_view` and
     `doe_dx_prepare_depth_view` (`runtime/c/backends/d3d12/src/windows_graphics.c:45-117`)
     allocate fresh RTV/DSV `ID3D12DescriptorHeap`s for every render-pass
     attachment.
   - `record_dispatch` (`runtime/c/backends/d3d12/src/windows_commands.c:176, 187`)
     and `draw_packet` (`runtime/c/backends/d3d12/src/windows_render_commands.c:112, 153`)
     call `transition_sets` (`runtime/c/backends/d3d12/src/windows_transitions.c:65-107`)
     before and after every dispatch or draw, emitting D3D12 resource barriers
     from `D3D12_RESOURCE_STATE_COPY_DEST` to the shader state and immediately
     back to `COPY_DEST`.
5. **Cold first-job latency (no persistent pipeline blob store on D3D12 and
   per-stage compiler DLL load/unload).** Forced Doe loses on cold first-job p50
   on four of six pages (1.09-1.45x):
   - `doeNativeDeviceConfigurePipelineBlobStore` (`runtime/c/native/src/pipeline_cache.c:116-161`)
     requires ELF build IDs (`lines 125-126`) and `WGPUBackendType_Vulkan`
     (`line 133`), rejecting D3D12 so compiled pipelines are not cached through
     the browser's blob store.
   - Every shader stage compilation (`compile_stage` in
     `runtime/c/native/src/d3d12_shader.c:73-148` → `compile_shader` /
     `compile_fxc` in `runtime/c/backends/d3d12/src/windows_shader.cpp:47-55, 138-255`)
     calls `GetModuleFileNameW`, `LoadLibraryExW`, `DxcCreateInstance` (or
     `D3DCompile`), and `FreeLibrary` instead of keeping `dxcompiler.dll` /
     `d3dcompiler_47.dll` and the DXC compiler/utils instances resident on the
     device or context.

## Limits

- One Mac (60 Hz display) and one Windows host (120 Hz display).
- Memory is a single sample at the end of the window. Where Doe's growth is
  linear (macOS before the fixes, and Windows D3D12), its value depends on the
  window length (2 s warm-up plus 6 s measured).
- An init-script observer and an extra rAF loop ran in every arm.
- Same-milestone baseline, but a different build configuration (official PGO vs
  Fawn's non-official build); the `fawn_dawn` control covers this.
- This is diagnostic evidence, not a claim-indexed row. It does not satisfy
  GOALS acceptance item 1 or the browser promotion contracts.

## Files

- `macos-summary.json` / `macos-summary.md`: all metrics, CIs, verdicts,
  failure, provider and position counts for the initial macOS run.
- `raw-evidence-macos.tar.gz`: per-launch specs and receipts, the schedule,
  the orchestration log, rep-0 screenshots and the harness source. Paths and
  user names are replaced with placeholders.
- `macos-after-summary.json` / `macos-after-summary.md` and
  `raw-evidence-macos-after.tar.gz`: the same for the macOS run after the fixes;
  the harness copy includes `config.macos.after.json`.
- `windows-summary.json` / `windows-summary.md` and
  `raw-evidence-windows.tar.gz`: the same for the Windows (D3D12) run; the
  harness copy includes `config.windows.json`.
- `SHA256SUMS`.

Reproduce, from a directory holding the harness:
`node orchestrate.mjs --config config.macos.json --out RUN` (or
`--config config.windows.json` on Windows), then
`python3 -I analyze.py RUN`.
