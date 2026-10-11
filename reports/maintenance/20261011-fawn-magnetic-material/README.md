# Black magnetic fluid with spectral reflections

Component: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Intent: preserved. Boundary effects: Magnetic Fluids fragment shading only.

The preceding shader applied its rainbow palette across the whole surface,
including flat regions. Its unnormalized reflected light direction also produced
a full-strength highlight on a flat normal, clipping much of the output and
washing the background pink. A gradient palette alone did not localize the color.

The corrected material uses a black body, surface-coordinate height derivatives,
and normalized lighting. Spectral gradients follow curved contours, surface
orientation and grazing-angle reflections. Flat regions stay black. This is a
stylized rendering choice, not a measured optical model of ferrofluid. The compute
shader, simulation parameters, controls and damped magnet response are unchanged.

[Pixel results](results.json) compare both actual fragment shaders against the
same GPU-computed field, using preceding source `1bd50de46`. The harness asserts
exact compute-shader identity. Flat, idle, pressed portrait and default 1024²-grid
fixtures check that flat output is black, most simulated surface remains dark,
visible red/green/blue reflection regions survive, and broad clipped highlights
are absent. PNGs are direct offscreen GPU texture readback rather than compositor
screenshots. The [idle comparison](idle-baseline.png) and [corrected idle image](idle-candidate.png)
show the material change; [portrait output](pressed-candidate.png) checks its shape
at an emulated mobile aspect ratio.

[Interaction results](interaction.json) exercise the actual application with the
independent ODE oracle, GPU state, mouse/touch release and cancellation, pause,
reset, resize, and the Backgrounds menu. These checks use Chrome WebGPU on the
test machine's physical Intel GPU. They do not qualify a physical phone,
forced-Doe browser release, or performance improvement.

Reproduce from the Doe root:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-magnetic-material.mjs
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-magnetic-transition.mjs
python3 bench/gates/catscan_gate.py
```

Pixel artifacts default to `bench/out/fawn-magnetic-material/`;
`FAWN_REPORT_DIR` selects another destination. Source and artifact hashes are
retained in `custody.json`. Hosting checks and publication identity are added
after deploying the source commit.
