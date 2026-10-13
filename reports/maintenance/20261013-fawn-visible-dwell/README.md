# Visible Fawn dwell and footer controls

Component: Fawn Chromium integration (hosted artwork)
Intent: changed by explicit user direction
Boundary effects: artwork formation schedule, parcel gathering and footer; no native runtime changes

The previous scene schedule left too little time for actual parcels to gather
before releasing the logo. Its formation-envelope assertions passed without
establishing a readable logo. The retained baseline run applies a stronger
position check to the published source and rejects it.

The cycle now supplies its own formation schedule to the shared controller,
and the resident parcel shader gathers faster from current positions. Shared
standalone formation timings remain unchanged. Scene cadence, effect order,
input-independent handoffs and transition attenuation remain unchanged.
The versioned policies own the timings and rates in
[`fawn-cycle-clock.js`](../../../browser/chromium/resources/fawn-cycle-clock.js)
and [`fawn-parcels.js`](../../../browser/chromium/resources/fawn-parcels.js).

Pause and Reset buttons are removed. Settings shares one footer row with FPS,
its graph and GitHub. The Settings panel opens above the footer and fits the
viewport. Keyboard motion controls preserve pause, reset and reduced-motion
accessibility without adding visible buttons.

## Verification

[`check-browser.mjs`](check-browser.mjs) reads actual GPU parcel positions and
compares each with its assigned authored logo target, before the fade and after
a dwell interval. It checks all effects, repeated scene boundaries, no reseeding,
input attenuation, mouse/touch holds, cancellation and keyboard pause. Footer
alignment and panel bounds are tested at desktop and narrow mobile sizes.

Reports retain exact source and producer hashes. Screenshots and raw captures
remain in ignored `bench/out/fawn-visible-dwell/`. These are software WebGPU
artwork diagnostics, not native Doe, physical-phone, Safari or performance
qualification. Hosted release identity remains `https://canvascontext.com/release.json`.

- [`baseline.json`](baseline.json): the published source fails the physical
  gathering check; [`check-initial.mjs`](check-initial.mjs) is its exact producer.
- [`footer-wrap-failure.json`](footer-wrap-failure.json): the first candidate
  passes desktop dwell but fails narrow-footer alignment; retained unchanged.
- [`browser.json`](browser.json): the final source passes physical dwell,
  repeated rotations and footer alignment on both viewport sizes.
- [`integration.json`](integration.json): transfer and advection oracles,
  per-parcel gathering, input, resize, lifecycle and terminal-error checks pass.
  This run began before the cosmetic footer wrap/formatting correction; the
  final browser receipt separately binds the corrected layout and runtime.
- [`timing.json`](timing.json): unchanged scene cadence and input attenuation,
  shared formation continuity, and the shorter cycle-only envelope pass.

- [`footer.json`](footer.json): the corrected graph flexes to keep the populated
  FPS counter, Settings and GitHub on one row; physical gathering/dwell still
  passes across every effect at desktop and narrow mobile sizes. This follows
  screenshot review that exposed wrapping after the initial counter updated.
