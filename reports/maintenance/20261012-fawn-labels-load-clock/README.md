# Fawn labels, default workload and scene clock

Component: Fawn preview artwork. Intent: changed names, reduced default work,
and interaction-independent scene timing. Boundary effects: Doe owns the artwork
and shared clock; D4DA checks and publishes the same canvas at all public aliases.
No native provider, compiler, browser-binary or model behavior changes.

The public names are Flows, Magnets and Particles; internal effect IDs and URLs
remain stable. The cycle uses its own workload preset so the retained standalone
benchmarks keep their earlier workload defaults. The shared particle count is
halved. Its fluid fields use a covering square grid; all conversion/resolve
invocations cover that entire grid rather than stopping at the parcel count.

Scene time uses visible elapsed time, independently of pointer input and the
clamped physics timestep. Input releases logo formation; releasing input allows
its idle formation ramp. Scenes continue their schedule while input is held,
without aborting a pending blend. Pause and hidden tabs retain their behavior.
Scene transitions still release the logo so the incoming material can circulate.

`cycle.json` binds source bytes, harness, browser and adapter. The desktop journey
uses the new default workload; the portrait journey checks the smaller selectable
workload. They cover complete rotations, finite fields, persistent buffers,
fixed projection, default allocation sizes, complete-grid conversion dispatches,
input-held scene completion, idle logo recovery, pause, hidden tabs and reset.
A lower-frame-rate case checks elapsed scene time rather than simulation time.
Independent numerical parcel and conversion references remain enforced.

This establishes application behavior and reduced declared work, not a measured
halving of total GPU time, physical-phone support or forced-Doe execution.
Screenshots remain outside Git. Earlier receipts retain their historical policies.

Reproduce from Doe:

```sh
node browser/chromium/scripts/build-fawn-shaders.js --check
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  FAWN_REPORT_DIR=/tmp/fawn-labels-load-clock \
  node browser/chromium/scripts/test-fawn-cycle.js
```

D4DA's `npm run test:fawn:hosting` checks all public aliases, updated labels,
default controls, real workload text and preservation of old model caches.
`/release.json` identifies the published source revisions.
