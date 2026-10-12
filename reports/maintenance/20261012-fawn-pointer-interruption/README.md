# Release interrupted particle gestures

Component: Chromium-hosted Fawn artwork.
Intent: preserved; interrupted input releases its gesture and allows branded
settling to resume without pausing or rebuilding the accepted world.
Boundary effects: CanvasContext's existing D4DA hosting projection publishes the
updated page. No shader, count, timestep, projection or native runtime changes.

Particles listened for canvas blur but did not listen for window blur. A held
mouse or Space gesture could remain active when the window lost focus. Flow and
Wave already clear that input at the window boundary. Particles now routes that
same event through its existing release handler.

`before.json` retains the reproduced failure. `after.json` records the repaired
mouse and keyboard boundary and checks the other backgrounds' behavior. The
check observes actual WebGPU uniforms and running state after dispatching a
window-blur event. It does not qualify physical operating-system focus behavior,
a custom Fawn binary or performance. The positive report identifies its browser
adapter; the earlier negative run is diagnostic and lacks adapter metadata.

Reproduce with `browser/chromium/scripts/test-fawn-pointer-interruption.mjs`.
`FAWN_PLAYWRIGHT_MODULE`, `FAWN_CHROME_ARGS`, `FAWN_REPORT_DIR` and
`FAWN_TEST_ORIGIN` select dependencies, browser setup, report and hosted origin.
