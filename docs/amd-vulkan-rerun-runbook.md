# AMD Vulkan Dawn-vs-Doe rerun runbook

This runbook defines the uncontended AMD Strix Halo (`amd-host`, Radeon 8060S,
Mesa RADV) rerun sequence once the protected host workloads finish and release
the GPU. It covers three sequential stages:

1. **Drop-in phase qualification** (`bench/tools/dropin_phase_cohort.py`) to
   verify the opt-in `DOE_VK_RESOURCE_AMD_CANDIDATE` (`0x1c`) candidate against
   both current `HEAD` (`0x3`) and the `profile == 0` reference baseline before
   changing `doe_vk_resource_profile_select`.
2. **Unchanged Doppler generation** (`bench/external-projects/doppler-generation`)
   to record matched Doe-versus-Dawn complete-operation latency, output
   correctness, cancellation/reuse, host RSS, requested/retained buffer bytes,
   and `amdgpu` VRAM/GTT telemetry, frozen before separate Apple Metal
   confirmation.
3. **Graphics presentation** (`bench/external-projects/d4da-world` and
   `bench/cli.py graphics`) to verify native render readback and visible
   swapchain presentation under matched Dawn and Doe controls.

Do not interrupt active foreign processes to force a run.

## 1. Demonstrated failure trace and candidate flags

The [`2026-10-29` drop-in phase report](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/README.md)
and [`round5/receipt.json`](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/receipt.json)
showed that the combined candidate beat Dawn on warm and complete operation
across all three workloads while failing two internal Doe guardrails against the
Round-5 `profile == 0` `main` baseline:

1. **`image_rgba_invert_1024` `teardownNs`:**
   - Measured `1.107 [1.086, 1.143]` (`480.58 us` vs `423.51 us`) in
     [`round5/combined/gate.out`](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/combined/gate.out)
     and `1.167 [1.117, 1.207]` in
     [`round5/m7r2/gate.out`](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/m7r2/gate.out)
     against the `1.05` `statedPhaseAllowances` ceiling.
   - Code-path trace in [`descriptors.c`](../runtime/c/backends/vulkan/src/descriptors.c),
     [`loader.c`](../runtime/c/backends/vulkan/src/loader.c), and
     [`memory.c`](../runtime/c/backends/vulkan/src/memory.c):
     - A `256`-set / `1024`-descriptor RADV `VkDescriptorPool` allocates an
       approx. `144 KiB` virtual-memory BO (`256 * 32 B` set headers + `136 KiB`
       mapped descriptor BO), crossing Mesa `radeonsi`/`radv` `pb_slabs`
       (`MAX_SLAB_ENTRY_SIZE = 128 KiB`) into direct `DRM_IOCTL_AMDGPU_GEM_CREATE`
       + `GEM_VA(UNMAP)` kernel teardown (`~35-50 us`) inside `wgpuDeviceRelease`.
     - Across 1 warmup + 32 timed iterations, `descriptors.c` still ran 33
       `vkAllocateDescriptorSets` + `vkUpdateDescriptorSets` +
       `vkFreeDescriptorSets` cycles even though all 33 dispatches bound the
       identical `(pipeline, in_buffer, out_buffer, params_buffer)` tuple, and
       `doe_vk_context_release` ran `doe_vk_pipeline_cache_clear` before
       `doe_vk_descriptor_blocks_clear`.
     - On current `HEAD`, `4 MiB` suballocation blocks (`<= 2 MiB` slice ceiling)
       exclude `image_rgba_invert_1024`'s `in_buffer` (`4 MiB`) and `out_buffer`
       (`4 MiB`), leaving 3 `DEVICE_LOCAL | HOST_VISIBLE` (`WC`) kernel BOs
       (`in_buffer`, `out_buffer`, and the `4 MiB` block holding `16 B` `params`)
       plus 1 `HOST_CACHED` `readback` BO (`4` total kernel `vkFreeMemory` calls
       at teardown).
2. **`vector_scale_add_262k` `warm.upload`:**
   - Measured `1.114 [1.108, 1.122]` (`23.50 us` vs `21.17 us`, `+2.33 us` on
     `2 x 1 MiB`) in [`round5/combined/gate.out`](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/combined/gate.out)
     and `1.112 [1.108, 1.116]` in
     [`round5/m2r2/gate.out`](../reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/m2r2/gate.out)
     against the `1.02` `uploadLatencyPhaseGate` ceiling (while complete warm
     operation improved to `0.630 [0.625, 0.635]` vs `main` and `0.619` vs Dawn).
   - Code-path trace in [`dropin_phase_harness.c`](../bench/drop-in/dropin_phase_harness.c)
     and [`resources.c`](../runtime/c/backends/vulkan/src/resources.c):
     - Between iteration `i` and iteration `i + 1`, `verify_vector` touches
       `1 MiB` `input_a[(i%2)]`, `1 MiB` `input_b[(i%2)]`, `1 MiB` `expected[(i%2)]`,
       and `1 MiB` `actual` (`4 MiB` working set), evicting `input_a[((i+1)%2)]`
       and `input_b[((i+1)%2)]` from Strix Halo's `1 MiB` per-core Zen 5 L2 into
       the `32 MiB` L3.
     - Under `profile == 0`, `doe_vk_buffer_write` runs `memcpy` from L3 into
       `HOST_CACHED` (`WB`) RAM (`43.13 B/ns` single-core STREAM triad ceiling).
       Under `DEVICE_LOCAL_HOST_WRITES`, `memcpy` streams `1 MiB` straight from
       L3 into uncached `WC` memory, contending L3 demand fills against the
       core's write-combining buffers (`WCB`s).

### Opt-in candidate controls (`doe_vk_resource_profile_select` unchanged)

Default selection in [`device_selection.c`](../runtime/c/backends/vulkan/src/device_selection.c)
remains unchanged (`0x3` on Strix Halo; `0x1` on `256 MiB`-BAR discrete AMD; `0x0`
elsewhere). Three opt-in candidate flags in [`doe_vk.h`](../runtime/c/backends/vulkan/include/doe_vk.h)
(`DOE_VK_RESOURCE_AMD_CANDIDATE = 0x1cu`) are exposed through
`doe_vk_options.{resource_profile_add, resource_profile_clear}` and native build
macros `VK_PROFILE_ADD` / `VK_PROFILE_CLEAR` in [`runtime/c/native/Makefile`](../runtime/c/native/Makefile):

- `DOE_VK_RESOURCE_DESCRIPTOR_REUSE` (`0x4u`): sizes descriptor blocks at `32`
  sets / `128` descriptors per type (`~18 KiB` RADV BO inside `pb_slabs <= 128 KiB`),
  retires written dispatches into a `16`-slot context cache with zero retained
  resource references and synchronous eviction on buffer, texture, view, sampler,
  or pipeline destruction, skips redundant `vkAllocateDescriptorSets`,
  `vkUpdateDescriptorSets`, and `vkFreeDescriptorSets` on matching bindings, and
  destroys descriptor pools before pipeline-cache teardown.
- `DOE_VK_RESOURCE_SUBALLOCATE_4MB` (`0x8u`): admits `<= 4 MiB` uncached
  (`!HOST_CACHED`) buffers into `12 MiB` suballocation blocks while keeping
  `HOST_CACHED` readback buffers on their existing `<= 2 MiB` / `4 MiB` rule,
  coalescing `image_rgba_invert_1024`'s three device-input/output buffers into a
  single `12 MiB` BO.
- `DOE_VK_RESOURCE_CHUNKED_WC_WRITE` (`0x10u`): stages `>= 64 KiB` writes into
  uncached `!HOST_CACHED` mappings through an aligned `8 KiB` L1D stack buffer
  in [`resources.c`](../runtime/c/backends/vulkan/src/resources.c) so L3-to-L1D
  reads decouple from L1D-to-WCB stores.

## 2. Host containment and uncontended preflight (`amd-host`)

Protected workloads observed on `amd-host` (`PID 204252`, `PID 1259994`) must
exit on their own before any GPU measurement begins. Never signal, renice, or
stop foreign processes without explicit owner direction.

Run this read-only check on `amd-host` before reserving a window:

```bash
ps -p 204252,1259994 -o pid,user,pcpu,pmem,etime,comm || true
systemctl list-units --type=service --state=running | grep -E 'reploid|doppler|ollama|vllm' || true
sudo -n sh -c 'cat /sys/kernel/debug/dri/*/clients' 2>/dev/null || lsof /dev/dri/renderD128 /dev/dri/card* 2>/dev/null || true
dmesg -T | tail -n 50 | grep -iE 'amdgpu.*(error|fault|hang|reset)' || true
reports/benchmarks/amd-vulkan/20261029-dropin-phase/round5/tools/quiet-check.sh true
```

Required preflight state before Stage 1, Stage 2, or Stage 3 starts:

1. Neither protected PID (`204252`, `1259994`) nor any foreign `amdgpu` DRM
   client holds `/dev/dri/renderD128`.
2. `dmesg` shows zero recent `amdgpu` GPU resets, page faults, or ring hangs.
3. `quiet-check.sh` reports `cpuBusyPercent <= 3.0` over a `10 s` window.
4. During every timed window, `window_gate.py` (or `run-campaign.py`'s built-in
   DRM monitor) records `foreignGpuClients == 0`, `foreignGpuBusyPercent <= 2.0`,
   and `foreignCpuPercentOfCapacity <= 5.0`.

## 3. Stage 1: Matched Dawn and Doe drop-in phase cohorts

Build four explicit Doe C shared libraries from the exact same checkout plus
pinned source-built Dawn (`d04f88359`, `bench/vendor/dawn/out/Release/libwebgpu_dawn.so`,
`sha256` `54040e4349ef6e9dc57d096b0563cdd1419c423ed353ff1c9552e70f6ddfb7f3`):

```bash
RUN_ROOT="$PWD/bench/out/amd-vulkan/$(date -u +%Y%m%d)-cand-rerun"
mkdir -p "$RUN_ROOT/builds" "$RUN_ROOT/harnesses" "$RUN_ROOT/raw" "$RUN_ROOT/summaries"

# 1. Current HEAD default (0x3 on Strix Halo)
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-main" \
  VK_PROFILE_ADD=0u VK_PROFILE_CLEAR=0u all

# 2. Pre-fccbd09f5 profile == 0 reference (per-submit pools + HOST_CACHED WB)
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-ref0" \
  VK_PROFILE_ADD=0u VK_PROFILE_CLEAR=0x3u all

# 3. Full AMD candidate (0x3 | 0x1c = 0x1f)
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-cand" \
  VK_PROFILE_ADD=0x1cu VK_PROFILE_CLEAR=0u all

# 4. Optional single-flag isolations if attribution is needed
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-cand-reuse" \
  VK_PROFILE_ADD=0x4u VK_PROFILE_CLEAR=0u all
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-cand-sub4m" \
  VK_PROFILE_ADD=0x8u VK_PROFILE_CLEAR=0u all
make -C runtime/c/native -j"$(nproc)" BUILD="$RUN_ROOT/builds/doe-cand-wc8k" \
  VK_PROFILE_ADD=0x10u VK_PROFILE_CLEAR=0u all
```

Prepare frozen inputs and link identical `dropin_phase_harness` binaries:

```bash
DAWN_SO="$PWD/bench/vendor/dawn/out/Release/libwebgpu_dawn.so"
sha256sum "$DAWN_SO" "$RUN_ROOT"/builds/*/libwebgpu_doe_c.so > "$RUN_ROOT/library-sha256.txt"

PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py prepare --out "$RUN_ROOT/inputs"

for pair in \
  "dawn:$DAWN_SO" \
  "dawn-aa:$DAWN_SO" \
  "doe-main:$RUN_ROOT/builds/doe-main/libwebgpu_doe_c.so" \
  "doe-ref0:$RUN_ROOT/builds/doe-ref0/libwebgpu_doe_c.so" \
  "doe-cand:$RUN_ROOT/builds/doe-cand/libwebgpu_doe_c.so"; do
  name="${pair%%:*}"
  lib="${pair#*:}"
  PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py build \
    --backend vulkan --library "$lib" --output "$RUN_ROOT/harnesses/$name"
  PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py build \
    --backend vulkan --upload-split --library "$lib" --output "$RUN_ROOT/harnesses/${name}-split"
done
```

Execute `192` balanced pairs (`32` timed iterations per process after 1 warmup)
inside gated uncontended windows, plus a `24`-pair `--upload-split` diagnostic
for `vector_scale_add_262k`:

```bash
for workload in \
  buffer_upload_readback_exact_1mb \
  vector_scale_add_262k \
  image_rgba_invert_1024; do
  PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py run \
    --inputs "$RUN_ROOT/inputs" \
    --workload "$workload" \
    --pairs 192 \
    --iterations 32 \
    --provider "dawn=$RUN_ROOT/harnesses/dawn" \
    --provider "dawn-aa=$RUN_ROOT/harnesses/dawn-aa" \
    --provider "doe-ref0=$RUN_ROOT/harnesses/doe-ref0" \
    --provider "doe-main=$RUN_ROOT/harnesses/doe-main" \
    --provider "doe-cand=$RUN_ROOT/harnesses/doe-cand" \
    --out "$RUN_ROOT/raw/$workload.jsonl"

  for control in dawn doe-ref0 doe-main; do
    PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py summarize \
      --raw "$RUN_ROOT/raw/$workload.jsonl" \
      --control "$control" \
      --out "$RUN_ROOT/summaries/${workload}-vs-${control}.json"
  done
done

PYTHONPATH=. python3 bench/tools/dropin_phase_cohort.py run \
  --inputs "$RUN_ROOT/inputs" \
  --workload vector_scale_add_262k \
  --pairs 24 \
  --iterations 32 \
  --provider "doe-ref0=$RUN_ROOT/harnesses/doe-ref0-split" \
  --provider "doe-main=$RUN_ROOT/harnesses/doe-main-split" \
  --provider "doe-cand=$RUN_ROOT/harnesses/doe-cand-split" \
  --out "$RUN_ROOT/raw/vector_scale_add_262k-upload-split.jsonl"
```

### Stage 1 promotion gate (prerequisite for changing `doe_vk_resource_profile_select`)

Promote `DOE_VK_RESOURCE_AMD_CANDIDATE` into `doe_vk_resource_profile_select` in
[`device_selection.c`](../runtime/c/backends/vulkan/src/device_selection.c) only if
all of the following hold across valid windows (`failedProcesses == 0`,
`dawn-aa/dawn` `warm.operation` median in `[0.95, 1.05]` with `1.0` inside CI95):

1. Versus `dawn`: `doe-cand/dawn` `complete` and `warm.operation` CI95 upper
   bounds `< 1.0` on all three workloads.
2. Versus `doe-ref0` and `doe-main`: `image_rgba_invert_1024` `teardownNs` ratio
   CI95 upper bound `<= 1.05` (and median `<= 1.05`).
3. Versus `doe-ref0` and `doe-main`: `vector_scale_add_262k` `warm.upload` ratio
   CI95 upper bound `<= 1.02` (or, if one candidate bit misses its target while a
   subset passes all guardrails, rebuild and re-verify that exact subset in a
   fresh `192`-pair cohort before promotion).

Select the winning Doe library (`$SELECTED_DOE_SO`) for Stages 2 and 3, rebuild
`packages/doe-gpu` against `$SELECTED_DOE_SO` (or pass `DOE_WEBGPU_LIB` via a
frozen provider manifest), and record its SHA-256.

## 4. Stage 2: Unchanged Doppler generation versus matched Dawn

Follow [`bench/external-projects/doppler-generation/README.md`](../bench/external-projects/doppler-generation/README.md)
against [`complete-generation-v2.json`](../bench/external-projects/doppler-generation/complete-generation-v2.json).
Do not alter Doppler's model, prompt, tokenizer, stopping logic, or WebGPU calls.

```bash
DOPPLER_DIR="$PWD/bench/external-projects/doppler-generation"
GEN_OUT="$RUN_ROOT/doppler-generation-v2"
MEM_OUT="$RUN_ROOT/doppler-memory-cohort"
mkdir -p "$GEN_OUT" "$MEM_OUT"

# 1. Generate or verify the independent CPU token/stopping oracle
python3 "$DOPPLER_DIR/reference.py" \
  "$DOPPLER_DIR/complete-generation-v2.json" \
  "$GEN_OUT/reference.json"

# 2. Write providers.json binding exact library paths and SHA-256 digests
#    (doe -> $SELECTED_DOE_SO, dawn -> $DAWN_SO, same Node binary and Doppler commit)

# 3. Qualify exact outputs, stopping reason, cancellation, subsequent reuse, and cleanup
node "$DOPPLER_DIR/run-generation.mjs" \
  "$DOPPLER_DIR/complete-generation-v2.json" \
  "$GEN_OUT/providers.json" \
  "$GEN_OUT/reference.json" \
  dawn "$GEN_OUT/dawn-qualification.json"

node "$DOPPLER_DIR/run-generation.mjs" \
  "$DOPPLER_DIR/complete-generation-v2.json" \
  "$GEN_OUT/providers.json" \
  "$GEN_OUT/reference.json" \
  doe "$GEN_OUT/doe-qualification.json"

# 4. Run the uninstrumented balanced complete-generation v2 campaign (refuses foreign DRM clients)
python3 "$DOPPLER_DIR/run-campaign.py" \
  "$DOPPLER_DIR/complete-generation-v2.json" \
  "$GEN_OUT/providers.json" \
  "$GEN_OUT/reference.json" \
  "$GEN_OUT"

# 5. Run the separate matched memory cohort (host RSS, requested/retained buffers, DRM VRAM/GTT)
python3 "$DOPPLER_DIR/run-memory-cohort.py" \
  "$DOPPLER_DIR/memory-contract.json" \
  "$GEN_OUT/providers-dawn.json" \
  "$GEN_OUT/providers-doe.json" \
  "$MEM_OUT"

python3 "$DOPPLER_DIR/analyze-memory.py" \
  "$DOPPLER_DIR/memory-contract.json" \
  "$MEM_OUT" \
  "$MEM_OUT/memory-report.json"

python3 "$DOPPLER_DIR/validate-memory.py" --local-archives "$MEM_OUT/memory-report.json"

# 6. Admit or reject the complete-generation baseline with bound memory report
python3 "$DOPPLER_DIR/analyze-campaign.py" \
  --root "$GEN_OUT" \
  --contract "$DOPPLER_DIR/complete-generation-v2.json" \
  --memory-report "$MEM_OUT/memory-report.json" \
  --out "$GEN_OUT/campaign-analysis.json"
```

### Stage 2 required evidence (freeze before Apple Metal confirmation)

1. `dawn-qualification.json` and `doe-qualification.json`: exact token stream and
   stopping match against `reference.json` across all three frozen Q4 Gemma
   cases, settled cancellation, clean subsequent generation on the reused
   provider, and clean device teardown without deferred-destruction warnings.
2. `campaign-analysis.json`: zero failed processes, zero foreign DRM clients
   around every sample, per-provider A/A intervals (`doe_aa_b/doe_aa_a` and
   `dawn_aa_b/dawn_aa_a`) inside `[0.95, 1.05]` with `1.0` inside CI95 for every
   case, plus complete-operation, load/preparation, first-token, and decode
   distributions.
3. `memory-report.json` validated by `validate-memory.py`: peak and post-cleanup
   host process RSS, requested/retained `GPUBuffer` bytes, active buffer count
   returning to baseline after cleanup, and Linux `amdgpu` VRAM/GTT snapshots.
4. Freeze the reviewed AMD Vulkan report and raw receipts under
   `reports/benchmarks/amd-vulkan/<date>-doppler-generation/` before running
   separate Apple Metal confirmation via [`metal-macos-proof-bundle-runbook.md`](metal-macos-proof-bundle-runbook.md).

## 5. Stage 3: Graphics submission, readback, and presentation

Keep native Vulkan library execution distinct from browser-owned page WebGPU
until direct Chromium wire ownership is implemented (`pageRuntimeOwnershipVerified`
in [`bench/external-projects/d4da-world/README.md`](../bench/external-projects/d4da-world/README.md)).

### 3A. Native Vulkan D4DA shader/render/readback check (available now)

Verify physical Vulkan render-pipeline compilation, draw submission, queue
completion, and mapped readback against `$SELECTED_DOE_SO`:

```bash
env -u VK_ICD_FILENAMES DOE_WEBGPU_LIB="$SELECTED_DOE_SO" \
  node bench/external-projects/d4da-world/check-mix-readback.mjs \
  > "$RUN_ROOT/d4da-native-mix-readback.json"
```

### 3B. Visible Three.js D4DA world presentation (`bench/cli.py graphics`)

Requires an active graphical display on `amd-host` (headless captures omit the
swapchain terrain and are rejected by the compositor PNG oracle):

```bash
# 1. Negative control: must fail live-canvas presentation verification
python3 bench/cli.py graphics \
  --source-root /path/to/d4da \
  --provider dawn \
  --negative-control omit-canvas \
  --out "$RUN_ROOT/graphics-dawn-omit-canvas" || true

# 2. Matched Dawn visible baseline (Solid, Translucent, Ghost modes)
python3 bench/cli.py graphics \
  --source-root /path/to/d4da \
  --provider dawn \
  --out "$RUN_ROOT/graphics-dawn"

# 3. Forced-Doe browser comparison (run only once browser pageRuntimeOwnershipVerified
#    is wired in Fawn Chromium; until then, --provider doe fails closed by design)
python3 bench/cli.py graphics \
  --source-root /path/to/d4da \
  --browser /path/to/fawn-chromium \
  --provider doe \
  --doe-library "$SELECTED_DOE_SO" \
  --out "$RUN_ROOT/graphics-doe"
```

### Stage 3 required evidence

1. Sensitivity of `--negative-control omit-canvas` (rejected as expected).
2. Independent planar-terrain CPU pixel oracle pass via mapped canvas readback
   plus live compositor PNG sample verification across Solid, Translucent, and
   Ghost modes.
3. Frame cadence (`p50`, `p95`, `p99`), preparation latency, CPU RSS, Linux DRM
   `amdgpu` VRAM/GTT memory counters, and explicit scene disposal + `GPUDevice.destroy()`
   teardown without validation errors or GPU resets.
4. Explicit provider-ownership proof (`Dawn` vs forced `Doe` with fallback
   disabled); never credit `check-mix-readback.mjs` native readback as browser
   swapchain presentation.
