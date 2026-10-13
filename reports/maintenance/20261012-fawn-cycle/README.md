# Continuous Fawn background cycle

Historical receipt: the user subsequently rejected this sheet-based formation
for its visible rectangle and zoom. These tests did not establish parcel motion
for the fluids. The [replacement evidence](../20261012-fawn-parcel-continuity/README.md)
checks actual parcel updates and fixed projection. Retained receipts are unchanged.

Component: Fawn preview artwork. Intent: changed to one rotating experience.
Boundary effects: Doe owns the cycle, shader projection and retained standalone
benchmarks; D4DA packages the new homepage and its imported modules. No native
Doe, compiler, browser-binary or model behavior changes.

The main page has one WebGPU canvas/device and one visible-time controller.
It holds Flow, Wave and Particles in order, with the durations declared by
`browser/chromium/resources/fawn-cycle-clock.js`. Interaction holds the schedule
and any current blend; pause and hidden tabs stop it. Reduced motion starts
paused. Pause, Reset, Settings, GitHub, completed-frame FPS and its graph remain.

GPU handoffs read accepted buffers and the preceding image. Flow dye and velocity
produce film height, motion and reflection. Film cells become particles at their
source coordinates, inheriting color and bounded motion. Bilinear fixed-point
particle deposits resolve pigment and velocity into the fluid grid. Handoffs do
not upload seed arrays, request another device, navigate, or read data back to the
CPU. The two materials render together only during the blend. Simulation
substeps share compute passes; offscreen targets and state buffers stay resident.

These are explicit artistic conversions between different state spaces. They
are not mass/energy-conserving physics or numerical equivalence between solvers.
The shared sheet transform carries formation geometry across effects. Particle
render exposure preserves inherited hue; bounded midpoint velocity advection retains
interior material instead of accumulating orbital acceleration into a ring.
The standalone pages and their historical shader evidence remain unchanged.

`cycle.json` binds the actual Chrome/adapter, source files and harness. It checks:

- GPU conversion outputs and midpoint advection against independent numerical
  references, including finite-difference derivatives of the stream function;
- complete rotations, finite fields, inherited interior particles, one device
  and canvas, and no new seed uploads or state-buffer allocations at handoffs;
- pressed-pointer interruption, pause, reset, hidden-page recovery, desktop and
  emulated phone rotation, and no horizontal overflow;
- first configuration at the canvas's default dimensions, DPR-only changes,
  reduced-motion startup and terminal GPU failure.

`rejected-perimeter.json` retains a rejected acceleration-based candidate whose
center-coverage assertion failed. The criterion remains enforced. A first
binding-layout error was repaired by making transfer grid bounds consume their
uniform contract; a resize fixture was corrected to await the browser's native
resize notification before advancing its controlled RAF. The cycle's passing
receipt does not establish subjective visual approval, physical phone support,
a forced-Doe Chromium artifact, or a performance advantage.

Shader definitions are projected from the retained demos by
`browser/chromium/scripts/build-fawn-shaders.js`; `--check` rejects stale bytes.
The hosting packager runs that check before copying the explicit module allowlist.

Reproduce from Doe:

```sh
node browser/chromium/scripts/build-fawn-shaders.js --check
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  FAWN_REPORT_DIR=/tmp/fawn-cycle-proof \
  node browser/chromium/scripts/test-fawn-cycle.js
python3 bench/gates/catscan_gate.py --root . --json
```

From D4DA, run `npm run type-check` and `npm run test:fawn:hosting` before
CanvasContext-only publication. `/release.json` identifies the published Doe and
D4DA commits. Browser images stay outside Git.
