# Magnetic fluid interaction transition

Component: [Fawn Chromium integration](../../../browser/chromium/CATSCAN.md).
Intent: preserved. Boundary effects: hosted background presentation and magnetic
pointer parameters; shader programs and solver ownership are unchanged.

The preceding application wrote pointer strength directly as zero, hover strength,
or full press strength. It also moved the field center directly to event coordinates.
The corrected application retains magnet position, strength and their velocities.
An exact critically damped response integrates them toward the current pointer
and engagement targets using elapsed seconds. Retargeting preserves velocity;
press, drag and release no longer switch force or position in a single frame.
Touch release and cancellation remove the force instead of leaving hover active.
Pausing freezes both the response and fluid simulation.

The three public background pages remove the instruction subtitle blocks and
rename their selector to Backgrounds, retaining the same menu destinations.

[Application results](results.json) reproduce the preceding strength jump,
compare the analytic response against an independently integrated RK4 acceleration
reference at multiple frame rates, and inspect the actual uniform inputs and GPU
fluid state across mouse and touch press/release/cancellation. Paused state and
small mobile background menus are checked too. These are Chrome WebGPU diagnostics
on the test machine's physical GPU with emulated mobile viewports, not physical
phone or forced-Doe browser qualification, and not performance measurements.

Reproduce from the Doe root:

```sh
FAWN_PLAYWRIGHT_MODULE=/path/to/playwright/index.mjs node browser/chromium/scripts/test-fawn-magnetic-transition.mjs
python3 bench/gates/catscan_gate.py
```

Set `FAWN_TEST_ORIGIN=https://canvascontext.com` to run against the public
backgrounds; that mode omits the local preceding-source reproduction. Generated
artifacts default to `bench/out/fawn-magnetic-transition/` and may be redirected
with `FAWN_REPORT_DIR`. Source and report hashes are retained in `custody.json`.

Publication succeeded at `https://canvascontext.com/` from Doe commit
`6edc4f33bb0c05150d1fb8f885a2267d88fd845f`. [Hosted identity](hosted/identity.json)
checks all eight package files and the root route against published bytes.
[Hosted application results](hosted/results.json) pass the same continuous input,
GPU state, desktop/touch and background menu checks against the public origin.
The local hosting smoke and successful publication logs are retained separately.
