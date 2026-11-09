# D4DA Three.js graphics workload

Runs the existing D4DA world page and its Three.js renderer without changing
application assets. Source commit, dirty state, served bytes, browser executable,
launch arguments, driver identity and generated WGSL are retained with each run.
The [plan](plan.json) owns the diagnostic cases and observation boundaries.

```sh
python3 bench/cli.py graphics --source-root /path/to/d4da \
  --out bench/out/graphics/run-id
```

Requires a graphical display, Chrome and Playwright (resolved from D4DA's installed
dependencies, or `DOE_PLAYWRIGHT_MODULE`). The output parent must exist; the run
directory must be new. The runner serves local application bytes and blocks
external requests, including analytics, recording those exclusions. It requires
WebGPU and rejects application fallback and software adapters. Headless Chrome on
the initial host completed GPU work while omitting the visible canvas; the runner
therefore uses a visible browser window. Retained failed runs remain diagnostics.
Plan v2 selects Metal on macOS, Vulkan on Linux and D3D11 ANGLE on Windows;
requested and actual browser arguments are retained. It keeps Chromium's sandbox
enabled. These platform choices do not establish qualified coverage on each OS.

The independent CPU oracle checks a planar fixture using the application's real
terrain material: palette texture upload, lighting, both triangles and time
invariance. Readback copies the actual canvas texture into a mapped buffer; it
does not use Canvas 2D interop. This does not qualify the full animated scene.

The full page runs Solid, Translucent and Ghost using its existing controls.
Raw samples distinguish application frame cadence, CPU callback work and elapsed
time through GPU queue completion. Every measured frame must submit work and
acquire a canvas texture. Screenshots retain visible composition. Preparation is
navigation to readiness after the material oracle, not cold-cache startup.
The observer follows the application's renderer factory and actual draw calls;
it does not depend on the runtime exporting its private animation callback or
renderer. The plan records both control values and the application's current
surface mapping: 1→1, 0.7→0.35 and 0.3→0.08. The observed draw must match that
declared mapping; historical plans retain their original interpretation.

After each timed batch, the harness holds one application frame, copies that
frame's canvas texture before yielding, and separately waits for queue
completion. It retains a PNG of the texture and composes those same bytes through
a temporary 2D canvas in the same DOM/CSS position. The actual WebGPU compositor
screenshot must match that reference at the sample points; the original canvas
is restored before resuming. This reference is needed because pointer-events-none
overlays affect pixels even though DOM hit-testing does not select them.
Two animation callbacks are only a compositor opportunity, not a presentation
receipt. Any intervening submission/acquisition or mismatched sample fails the
run. The frame sequence, application state, camera matrices, raw samples,
screenshot hash and sample coverage
are retained. This does not prove redaction, semantic scene correctness or
physical display scanout. The separate planar material oracle remains the
independent correctness reference.

Presentation checks require an opaque, unscaled 8-bit canvas at DPR 1 and run
outside timed batches. Blank, stale, wrong-color and changed-frame rejection are
covered by the focused checks. Missing prerequisites fail instead of accepting
an unverified screenshot.
`--negative-control omit-canvas` makes only the actual compositor capture
transparent, after queue completion, and restores it before the reference
capture. That run must fail the pixel oracle, retaining the failed receipt.

Process CPU counters, RSS and Linux DRM client counters are checkpoint samples;
they do not establish peak physical GPU memory. Explicit scene disposal checks
device destruction separately from browser process shutdown.

`--provider doe --browser PATH --doe-library PATH` checks the browser and library
paths, then refuses execution because native page ownership verification is not
yet implemented here. Requested switches cannot prove ownership or exclude
fallback. Every receipt remains `claimStatus=diagnostic` and
`pageRuntimeOwnershipVerified=false`; this tool cannot admit a provider win.
The missing forced-Doe browser, independent whole-scene correctness, deterministic
cross-provider inputs, timing observer calibration, independent process sampling
and display presentation timing remain separate acceptance work. A passing run
means its explicitly scoped checks completed, not that Doe beats Dawn.

Focused harness verification:

```sh
node bench/external-projects/d4da-world/test.mjs
```

Retained captures can be fed unchanged to `doe-emit-spirv` and `spirv-val` to
find compiler failures. Successful compilation is not GPU output equivalence.
The scalar-factor `mix` correction also has a focused physical Vulkan fragment
check, using CPU-expected pixels and an alpha channel that detects repeated
factor evaluation:

```sh
DOE_WEBGPU_LIB=/absolute/path/libwebgpu_doe_full.so \
  node bench/external-projects/d4da-world/check-mix-readback.mjs
```

This uses the existing native addon, verifies the loaded library through Linux
process maps, and checks render submission, completion and mapped texture bytes.
It is a native compiler regression, not the unchanged browser application proof.
