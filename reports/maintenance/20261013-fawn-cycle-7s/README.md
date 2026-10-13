# Scheduled Fawn crossfades

Component: Fawn Chromium integration (hosted artwork)
Intent: changed by explicit user direction
Boundary effects: artwork clock and input uniforms; no native runtime or shader changes

The scene schedule now advances independently of mouse movement, presses,
held touches, and magnetic release pulses. The versioned
[`CYCLE_POLICY`](../../../browser/chromium/resources/fawn-cycle-clock.js) owns
cadence, crossfade duration, and minimum input strength. During each crossfade,
a symmetric raised-cosine curve softens interaction and restores it smoothly.
Fluid stirring and dye injection, magnetic attraction, particle repulsion,
and charged release pulses use that same envelope. Coordinates, drag paths,
formation progress, and resident moving material are preserved.

Pause and hidden-tab behavior retain their existing visible-time boundary.
Logo recovery still uses its own quiet interval; it cannot delay scene changes.
This supersedes the [input-deferral policy](../20261013-fawn-cycle-24s/README.md),
whose artifacts remain historical evidence.

## Policy migration

Scene-clock policy advances to version 7 and removes the input quiet guard.
The formation controller still owns logo recovery’s quiet interval. The GPU
uniform owner advances to version 8 with an optional interaction-scale input;
callers omitting it retain full-strength interaction. No shader uniform layout
or application controls change.

## Verification

- [`timing.json`](timing.json): schedule boundaries with early, late and
  continuous input; full rotations; bounded symmetric input curve with smooth
  endpoints; preserved formation recovery and spring continuity.
- [`browser.json`](browser.json): source- and harness-bound software WebGPU
  execution on desktop and mobile-size viewports. Repeated boundaries retain
  allocations and uploads; readback remains finite. Held mouse and touch input
  keep the rotation on schedule. Uniform observations check fluid force,
  magnetic attraction, particle repulsion and the attenuated release pulse.
  Cancellation and pause preserve their existing behavior.
- [`integration.json`](integration.json): the existing integration harness checks
  independent transfer/advection/formation oracles, default buffer allocations,
  input, reset, rotation, pause, hidden tabs, reduced motion, DPR changes,
  lower-frame-rate timing and terminal errors. Execution uses the explicitly
  selected smaller software workload recorded in the receipt. The earlier
  full-size software attempt was stopped before completion; it is not default
  workload performance evidence.
- [`check-browser.mjs`](check-browser.mjs) is the retained diagnostic producer.
  Raw runs and screenshots remain under `bench/out/fawn-cycle-7s/` and are not
  added as compressed test captures.

These are artwork diagnostics using software WebGPU and emulated input, not
physical-phone, Safari, native Doe or performance qualification. The live
[`release.json`](https://canvascontext.com/release.json) remains publication
identity; local source changes do not replace its deployed bytes.

```bash
node browser/chromium/scripts/test-fawn-timing.js
node browser/chromium/scripts/build-fawn-shaders.js --check
FAWN_PLAYWRIGHT_MODULE=/absolute/path/to/playwright-core/index.mjs \
FAWN_REPORT_DIR=bench/out/fawn-cycle-7s \
node reports/maintenance/20261013-fawn-cycle-7s/check-browser.mjs
```

For the integration diagnostic, `FAWN_JOURNEY_COUNT` explicitly selects the
particle count and smaller diagnostic viewports at initial device scale one.
Default allocation checks still run before selecting that workload. With no
override, the original full-size journeys and default counts remain selected.

```bash
FAWN_PLAYWRIGHT_MODULE=/absolute/path/to/software_playwright_adapter.mjs \
FAWN_JOURNEY_COUNT=1024 \
FAWN_REPORT_DIR=bench/out/fawn-cycle-7s/integration \
node browser/chromium/scripts/test-fawn-cycle.js
```

The adapter selects installed Chrome with `--enable-unsafe-webgpu` and
`--use-angle=swiftshader`; the receipt records the observed provider.
