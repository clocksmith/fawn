# Magnetic relief and scene dwell

Component: Fawn Chromium integration (hosted artwork)
Intent: changed by explicit user direction
Boundary effects: artwork rendering and scheduling; no native runtime changes

The preceding renderer averaged magnetic surface detail into transported parcel
moments. That erased narrow bubble rings. The frozen-geometry field ablation in
[`baseline.json`](baseline.json) retains the published source's rejection:
removing the live magnetic field did not change the displayed material.

The renderer now samples the active magnetic solver field for soap-film normals,
spectral contours and reflection bands, with coverage supplied by the moving
parcels. Both ping-pong fields remain resident; rendering follows the active one.
The logo boundary still permits interior advection and live material updates.
Flow and magnetic light blend before exposure, and film/point footprints blend
at the same parcel positions. There is no new canvas, stamp or scene reseed.

`CYCLE_POLICY` in `fawn-cycle-clock.js` owns the requested cadence and longer
formation opportunity. Input cannot reset that cadence; its force softens along
the existing transition curve. Scene order, device counts, solver steps and
footer controls are preserved.

## Verification

[`check-browser.mjs`](check-browser.mjs) freezes source bytes and advances the
visible clock while reading actual GPU state. Each scene must gather material
into the authored silhouette, keep it moving there, update material and execute
live solver steps. Every blend is sampled for incoming visibility and a dark dip.
Ordinary scene boundaries retain allocations and uploads.

After that journey, a paused magnetic-field ablation freezes parcel geometry,
removes the live solver field, then restores its exact bytes. Displayed pixels
must respond to removal and return to their prior appearance after restoration.
These deliberate diagnostic uploads are separate from ordinary transition checks.

- [`browser.json`](browser.json) retains the corrected source at a diagnostic
  workload on desktop and narrow mobile viewports.
- [`integration.json`](integration.json) retains independent shader/transfer
  references, branded advection, lifecycle, input-held rotation and resize checks.
- [`timing.json`](timing.json) checks the cadence and input-independent clock.

Run from the Doe project root:

```sh
FAWN_PLAYWRIGHT_MODULE="$PWD/browser/chromium/node_modules/playwright-core/index.mjs" FAWN_REPORT_DIR=bench/out/fawn-eight-second-scenes/candidate node reports/maintenance/20261013-fawn-eight-second-scenes/check-browser.mjs
FAWN_DEFAULT_COUNTS=1 FAWN_PLAYWRIGHT_MODULE="$PWD/browser/chromium/node_modules/playwright-core/index.mjs" FAWN_REPORT_DIR=bench/out/fawn-eight-second-scenes/default-counts node reports/maintenance/20261013-fawn-eight-second-scenes/check-browser.mjs
node browser/chromium/scripts/test-fawn-timing.js
node browser/chromium/scripts/build-fawn-shaders.js --check
```

Screenshots stay in
ignored `bench/out/` and are reviewed locally. These checks are Chrome software
WebGPU artwork diagnostics, not native Doe, physical-phone, Safari or performance
qualification. Hosted publication requires its separate byte and browser checks.
