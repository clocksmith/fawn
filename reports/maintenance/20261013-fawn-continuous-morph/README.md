# Continuous per-particle formation

Component: Fawn Chromium integration (local hosted-artwork source)
Intent: corrected from user-reported formation defects
Boundary effects: parcel morph, shared logo constraint and formation timing

The boundary-only collector stopped guiding parcels already inside the mark.
That let their fields crowd its outline and left Particles poorly distributed.
Rapid collection also read visually as a fluid-filled logo appearing in place.

Each parcel now records its prior morph progress and advances a positional
interpolation from its current trajectory to its assigned destination. Its RGB
pigment is not faded. The private color record's fourth channel stores progress;
the renderer still computes optical coverage independently from live splats.
Staggered arrival preserves individual paths without changing the viewport.

A smooth completed endpoint lets the live field take over rather than repeatedly
pulling back an asymptotically converging point. Gentle interior attraction keeps
the mark filled while field advection, material updates and the same boundary
projection continue for Flows, Magnets and Particles. The versioned clock policy
owns the longer formation ramp; the scene cycle and input-independent clock remain.

## Evidence

- [`baseline.json`](baseline.json) substitutes only the previously published parcel kernel in the
  controlled shader oracle. Its positional-trajectory mismatch retains the
  original failure; this is not a requalification of the whole old application.
- [`tight-anchor-failure.json`](tight-anchor-failure.json) and
  [`asymptote-failure.json`](asymptote-failure.json) retain rejected local
  candidates under unchanged live-motion acceptance.
- [`check-browser.mjs`](check-browser.mjs) snapshots source bytes and checks actual GPU silhouette
  occupancy, continued motion, material updates, every blend, allocation/upload
  continuity and magnetic-field rendering on desktop and narrow mobile viewports.
  Its magnetic ablation waits on uniform state instead of repeatedly copying all
  parcel data; the removal/restoration acceptance is unchanged.
- [`browser.json`](browser.json) binds the final local source to those rendered
  and live-state checks; [`integration.json`](integration.json) retains numerical
  references, input-held rotations, pause, resize, reset and lifecycle checks.
- [`defaults.json`](defaults.json) checks the same final source, moving silhouettes
  and every blend at the normal desktop and mobile particle counts.
- [`timing.json`](timing.json) checks the preserved scene clock and longer formation.

The existing `test-fawn-cycle.js` adds a controlled positional-interpolation
reference with unchanged RGB, alongside independent advection/transfer references
and lifecycle journeys. `FAWN_ORACLE_ONLY=1 FAWN_PARCEL_BASELINE=4ec2ed765` reproduces
the old kernel rejection. Ordinary full journeys retain their original scope.

These are local Chrome software WebGPU artwork checks, not native Doe,
physical-phone, Safari or performance qualification. Screenshots remain in ignored
`bench/out/fawn-continuous-morph/`. This correction has not been deployed.
