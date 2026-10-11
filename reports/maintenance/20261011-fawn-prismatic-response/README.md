# Prismatic fluid input response

Component: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Intent: preserved. Boundary effects: Prismatic Fluids pointer response and frame
admission only; other backgrounds and hosting routes are unchanged.

The preceding application treated accumulated pointer displacement as a force
and multiplied it by the frame duration again. This made first-frame movement
weak, then replayed the same input with a 0.72 decay in later frames. The pointer
also affected only a disk around its final position, leaving fast strokes behind.
A complete touch tap between frames could produce no press response.

The corrected application consumes drag displacement once as an immediate impulse
and applies velocity and dye along the swept segment. Press strength, sustained
swirl, and color deposition are stronger. Presses carry a one-time outward impulse
and color burst until the next simulation frame, including quick taps. Velocity
injection has an explicit speed limit; release and cancellation cannot leave an
active touch force. One frame may remain in flight, reducing queued stale input.
The pressure solver, default grid, controls and background styling retain their
existing responsibilities.

[Response results](results.json) bind preceding and corrected source to actual
GPU readback at the default 512² simulation grid and 24 pressure iterations.
Desktop mouse and emulated mobile touch episodes check first-frame drag strength
across the full stroke, a consumed displacement on the following frame, stationary
press and between-frame tap responses, stronger dye, release/cancellation and
finite velocity/dye after continued simulation. Acceptance compares the same
input sequence with the preceding source; it does not infer an FPS or physical
end-to-end latency improvement from simulation amplitudes.

[Control results](controls.json) retain the existing desktop/mobile layout,
paused GPU work, reset, settings, resize, reduced motion and unavailable-GPU
checks. These are Chrome application diagnostics on the test machine's physical
GPU with emulated viewports, not physical phone or forced-Doe browser qualification.

Reproduce from the Doe root:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-prismatic-response.mjs
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-prismatic-controls.mjs
python3 bench/gates/catscan_gate.py
```

The retained control check used physical-Vulkan Chrome flags rather than the
machine's default headless graphics path. Response artifacts default to
`bench/out/fawn-prismatic-response/`; `FAWN_REPORT_DIR` changes their destination.
Set `FAWN_TEST_ORIGIN=https://canvascontext.com` for the same response checks
against the public page, omitting the local preceding-source comparison.

Publication succeeded at `https://canvascontext.com/` from Doe source commit
`2aa745fe315fbf5354ebc0694b34b29f53193aae`.
[Hosted identity](hosted/identity.json) verifies all eight package files and the
root route against the published bytes. [Hosted response](hosted/results.json)
passes the first-frame stroke, quick tap, release/cancellation and finite GPU
state checks for desktop mouse and emulated mobile touch on the public page.
Local hosting smoke and successful deployment logs are retained separately.
