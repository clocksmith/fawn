# Fawn parcel continuity repair

Component: Fawn preview artwork. Intent: individual material motion replaces the
rejected sheet transition. Boundary effects: Doe owns the single-canvas artwork;
D4DA publishes its modules and aliases legacy demo URLs. No native compiler,
provider, browser binary, or model behavior changes.

The reported square and zoom came from four sheet vertices moving into the logo
bounds, followed by a silhouette mask. The preceding cycle receipt accepted that
mechanism; it did not prove particle-by-particle fluid formation. That receipt and
its source history remain retained.

The replacement has one persistent 48-byte parcel population through Flow, Wave,
and Particles. The live Flow velocity/dye/curl and Wave height/reflection fields
drive parcel motion and material. Each parcel moves from its own accepted position
toward an SVG target with a stable, staggered arrival rate. Neither the camera nor
the render quad depends on formation strength. A circular initial reservoir extends
past viewport corners, avoiding a visible rectangular grid boundary.

Flow -> Wave initializes the film field from accepted Flow data. Wave -> Particles
only changes field/material weights, preserving every parcel. Particles -> Flow
scatters carried pigment and velocity into the Flow grid. Shared circular splats
accumulate bounded color/material moments; a fixed full-canvas pass lights those
moments. This is an artistic hybrid, not mass/energy-conserving fluid physics.

`cycle.json` binds the source bytes, harness, browser and adapter. Checks include:

- independent GPU references for all three formations, irregular input positions,
  preserved carried material, and finite-difference midpoint advection;
- independent Flow-to-Wave and parcel-to-Flow conversion references;
- fixed projection through settling and initial coverage beyond square corners;
- complete rotations on desktop and an emulated portrait viewport, finite fields,
  resident buffers, one canvas/device, and no seed uploads at handoffs;
- pointer interruption, pause, reset, hidden tabs, rotation, DPR-only resize,
  reduced-motion startup and terminal failure.

Default-density visual inspection covered free, partially settling and settled
Flow, Wave and Particles. Screenshots remain outside Git. Chrome checks and
emulated viewports do not establish physical-phone support, forced-Doe execution,
subjective user approval, or a performance advantage.

Reproduce from Doe:

```sh
node browser/chromium/scripts/build-fawn-shaders.js --check
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  FAWN_REPORT_DIR=/tmp/fawn-parcel-proof \
  node browser/chromium/scripts/test-fawn-cycle.js
python3 bench/gates/catscan_gate.py --root . --json
```

D4DA's `npm run test:fawn:hosting` checks all five public entry URLs as the same
single-canvas experience without a background selector. It also checks controls,
excluded benchmark pages and retirement of the old Doppler worker while preserving
model caches, local storage and OPFS. `/release.json` identifies published commits.
