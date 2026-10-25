# Paired MSAA scissor diagnostic

[The receipt](MSAA_SCISSOR_DIAGNOSTIC.json) records a **correctness failure on
both providers**, not a passed acceptance gate. On the same single-threaded CPU
Vulkan driver, retained Zig and C produced byte-identical complete 64-pixel
readbacks: the eight pixels at x=3 were `(128,0,128,255)` rather than blue.
The expected scissor covers x=4..7. Zero tolerance remains required.

This establishes matching behavior for this particular symptom; it does not
establish universal parity or physical GPU correctness. The receipt binds the
fixture/helpers/header, provider builds, driver archive/library and pixel log by
SHA-256. It includes every actual/expected pixel, not only a summary or hash.

The unchanged `tests/msaa_sampling.c` still fails. C reaches its masked-sample
comparison at line245; Zig fails earlier at line79 because it admits a layout the
negative control requires it to reject. That earlier failure was **not skipped**.
The separate positive-path fixture isolates public render/scissor/resolve/readback
without changing any assertions in that frozen consumer. A separate raw-Vulkan
reproduction had the same symptom; no speculative driver workaround was added.

## Reproduce explicitly

Use Linux, a C compiler, the two named provider builds, and an explicitly selected
Vulkan CPU ICD. Build the retained provider from the receipt's commit with pinned
Zig0.15.2 (`zig build dropin -j1 -Doptimize=ReleaseFast` under `runtime/zig`);
use normal explicit `--prefix`/`--search-prefix` options for local dependencies.
The C provider requires no Zig. Supply absolute existing library paths as `C_LIB`
and `ZIG_LIB`, and an ICD manifest as `VK_ICD_FILENAMES`. From the repository root:

```sh
: "${C_LIB:?set the C shared-library path}"
: "${ZIG_LIB:?set the retained Zig shared-library path}"
: "${VK_ICD_FILENAMES:?select the CPU Vulkan ICD}"
export VK_ICD_FILENAMES
export DOE_C_TEST_ADAPTER=software DOE_C_TEST_BACKEND=vulkan
export DOE_BACKEND=vulkan FAWN_BACKEND_LANE=vulkan_doe_release
export LP_NUM_THREADS=1 MESA_SHADER_CACHE_DISABLE=true
SCRATCH=$(mktemp -d)
ulimit -c 0
ulimit -v 2097152
ulimit -t 25
```

Run these **separately**; on the recorded driver each executable exits1 and make
exits2. A failing first invocation must not prevent recording the second one:

```sh
timeout --kill-after=5s 30s make -C runtime/c/native -j1 \
  BUILD="$SCRATCH/c" MSAA_DIAGNOSTIC_LIBRARY="$C_LIB" \
  check-msaa-scissor-diagnostic > "$SCRATCH/c.log" 2>&1
```

```sh
timeout --kill-after=5s 30s make -C runtime/c/native -j1 \
  BUILD="$SCRATCH/zig" MSAA_DIAGNOSTIC_LIBRARY="$ZIG_LIB" \
  check-msaa-scissor-diagnostic > "$SCRATCH/zig.log" 2>&1
```

Distinct build directories prevent stale relinking when selecting providers.
Compare lines beginning `pixel ` and `exact64-pixel`; compiler command lines may
differ. The fixture prints all pixels, cleans up, then fails if **any** expected
pixel differs. The optional target is not a dependency of default `check` and
neither removes nor substitutes any existing MSAA test. Keep failure exit codes
and logs when recording another driver; identical failures are not a pass.
