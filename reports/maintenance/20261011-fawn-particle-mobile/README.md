# Fawn particle viewport repair

Owner: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Source: [main particle page](../../../browser/chromium/resources/fawn-start.html).

The previous flow drove particles into viewport walls. Clamping their positions
outside the visible canvas erased their spatial differences, so later fields
moved a single collapsed cloud. The main page now uses normalized viewport
positions, a circulating stream-function field, soft inward edge forces and
reflection that retains collision overshoot. Sprite size still respects canvas
aspect. A canvas size observer recreates drawing targets without resetting
particle state.

[Application results](results.json) bind preceding and corrected HTML sources to
GPU readback across repeated cycles, desktop and emulated phone viewports,
rotation while paused, touch release, reset and keyboard controls. Exact particle
state digests establish viewport independence. PNG files ending in `.canvas.png`
come from the GPU trail target, with explicit row-stride and BGRA conversion;
layout screenshots observe browser chrome separately.

[Field results](particle-fields.json) compare GPU execution against an independent
CPU reference, using finite differences of the stream function rather than copying
its shader derivatives. Field phases, blends, pointer forces, viewport aspects,
speed limits, both wall axes and overdispatch are checked against the retained
absolute tolerance. This replaces the earlier equivalence-to-frozen-shader
expectation for the deliberately corrected application fields; historical reports
retain their original shader and verdict.

Reproduce from the repository root with an installed Playwright ESM module:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-particle-mobile.mjs
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-particle-fields.mjs
python3 bench/gates/catscan_gate.py
```

Artifacts default to `bench/out/fawn-particle-mobile/` for the viewport check;
set `FAWN_REPORT_DIR` to the same directory for the field check. The viewport
check requires Chrome and a physical WebGPU adapter and uses a controlled frame
clock. Its displayed frame rate is not a performance measurement. These are
application correctness diagnostics on Chrome's WebGPU provider, not physical
phone qualification, a forced-Doe browser release, or deployment evidence.

The D4DA Fawn package was built from source commit
`7060626cf21b1e40a3cafbae530ffbea1513f30f`. Its hosting smoke check passed with
physical-GPU Chrome flags, including all three public pages and retirement of
the old worker while preserving user data. The default headless launch timed
out; both attempts are retained. Publication to `canvascontext-9da05` was
attempted with the existing Fawn deploy command and a cached Firebase CLI,
but Firebase rejected authentication. The corrected page is not claimed live.
See `hosting-smoke-default.log`, `hosting-smoke-physical.log`,
`deploy.log`, and `package-release.json`.
