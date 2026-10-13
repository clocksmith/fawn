# Living logo and material transitions

Component: Fawn Chromium integration (hosted artwork)
Intent: changed by explicit user direction
Boundary effects: artwork simulation, parcel boundary and material lighting; no native runtime changes

The prior branded envelope stopped field timesteps, suppressed material updates,
and pulled parcels toward fixed targets. Its dwell check established a static
silhouette but did not establish live simulation. The baseline receipt applies
live-motion acceptance to that published source and preserves its failures.
The fluid-to-film appearance also suppressed film with little relief, causing a
dark handoff despite resident material.

Branding now gathers outside material from its current positions, then constrains
only the silhouette boundary. A rasterized signed distance field removes outward
velocity near the edge and projects escaped captured parcels back into the mark.
Interior parcels continue tracing the live fluid, magnetic or particle field;
material colors and relief continue updating. Field timesteps and simulation time
remain active throughout branding. The boundary is resident, updated on resize,
and uses the supported storage-binding budget.

Fluid shading blends carried lighting before exposure and includes film pigment
and ambient illumination even on flat relief. Surface and point kernels interpolate at each live parcel position, preserving
coverage as point footprints replace the broader surface footprint. Appearance
continues interpolating through the same population. No scene reset, CPU readback,
new animation canvas, or model/provider behavior is introduced.
The existing scene order, cadence, input attenuation, keyboard controls and footer
are preserved. Versioned policies own the new boundary and lighting parameters.

## Verification

[`check-browser.mjs`](check-browser.mjs) serves a cached source snapshot and reads
actual GPU state. Every effect must gather into the silhouette, remain inside it,
continue moving, update its material, and keep live solver timesteps. Pixel
samples across every transition check visible incoming material and no black dip.
All scene boundaries retain allocations and uploads. Screenshots remain in
ignored `bench/out/fawn-living-logo/` and are reviewed locally.

The existing integration harness retains independent transfer and advection
references and now verifies branded interior advection, changing material and
boundary-tangent flow. Lifecycle, interaction and default allocation checks
remain. These are software WebGPU artwork diagnostics, not native Doe,
physical-phone, Safari or performance qualification.

- [`baseline.json`](baseline.json) retains frozen-branded and dark-film failures
  from the previously published source.
- [`browser.json`](browser.json) checks actual moving material, silhouette
  retention and transition pixels on desktop and narrow mobile sizes.
- [`integration.json`](integration.json) retains independent shader references,
  live branded/tangent advection and the existing complete lifecycle journey.

- [`footprint-dip-failure.json`](footprint-dip-failure.json) retains the normal
  desktop-count rejection: shrinking a single splat radius lost coverage during
  the film-to-point transition. Interpolated endpoint kernels repair that boundary.

- [`footprints.json`](footprints.json) checks the corrected kernel interpolation
  alongside live branded motion and all transition pixels. The earlier integration
  receipt predates this rendering correction; its numerical/lifecycle scope is
  preserved, while this receipt binds the corrected rendering source.

- [`defaults.json`](defaults.json) binds the final source to moving branded
  silhouettes and transition pixels at the normal desktop and mobile counts.
  All effects and handoffs pass. Local screenshots were inspected; captures stay
  outside version control.

- [`timing.json`](timing.json) verifies unchanged cadence, transition input
  attenuation and formation-controller continuity.
- [`deployment.json`](deployment.json) binds successful preview/live publication,
  exact packaged/hosted bytes and hosted Chrome checks to the tested code revision.
