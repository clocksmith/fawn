# Fawn cycle continuity and ambient shimmer

The scheduled formation target previously fell directly to zero when an effect
blend began. Its faster response released the shape ahead of the appearance
blend. The host also admitted unbounded elapsed frame time, allowing a suspended
frame to skip a visible blend and the handoff preparing the next simulation.

Formation release now follows the effect blend from its current strength while
preserving response velocity. Pointer interaction retains its independent release
and recovery. The host bounds catch-up after a stall; ordinary scene timing,
pause and resident parcel ownership retain their existing behavior.

Flow's ambient glint strength increases independently of foreground glints and
fluid saturation. [Matched renderer comparisons](comparison.json) retain actual
pixel differences for desktop and emulated mobile, brighter animated empty
backgrounds and unchanged Wave/Particles pixels.

- [Preceding failure](before.json): the continuity check rejects abrupt formation release.
- [Corrected repeated cycles](after.json): actual WebGPU parcel readback, scene
  boundaries, repeated particle-to-flow wraps, no reseeding or scene allocation,
  stalled-frame handling, pointer recovery and paused state.
- [Review](review.json): final source identities, checks and evidence limitations.

Reproduce from the Doe root with an installed Playwright ESM module:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs \
  node browser/chromium/scripts/test-fawn-cycle-continuity.js
node browser/chromium/scripts/build-fawn-shaders.js --check
```

Set `FAWN_BASELINE_REF=68e0ff8e4` and a different `FAWN_REPORT_DIR` to reproduce
the preceding rejection. The controlled full-loop check uses a reduced parcel
count and software WebGPU. Material comparisons use the ordinary desktop/mobile
counts. These are artwork diagnostics, not physical-device, Safari, forced-Doe
browser or performance qualification. Visual preference remains subject to review.
