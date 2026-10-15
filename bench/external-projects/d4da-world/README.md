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

The independent CPU oracle checks a planar fixture using the application's real
terrain material: palette texture upload, lighting, both triangles and time
invariance. Readback copies the actual canvas texture into a mapped buffer; it
does not use Canvas 2D interop. This does not qualify the full animated scene.

The full page runs Solid, Translucent and Ghost using its existing controls.
Raw samples distinguish application frame cadence, CPU callback work and elapsed
time through GPU queue completion. Every measured frame must submit work and
acquire a canvas texture. Screenshots retain visible composition. Preparation is
navigation to readiness after the material oracle, not cold-cache startup.
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
