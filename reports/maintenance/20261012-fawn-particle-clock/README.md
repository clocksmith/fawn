# Particle input follows model time

Component: Chromium-hosted Fawn artwork.
Intent: preserved; pause freezes the accepted model, and input timing follows
model time rather than the number of rendering callbacks.
Boundary effects: CanvasContext's existing hosting projection publishes the page.
No shader, count, projection, native runtime or browser-binary changes.

The pointer-force filter advanced on every redraw, including paused redraws.
Its response also varied with the rendering cadence. It now uses a versioned,
named response policy and an exponential update from the frame's model timestep.
The reference response and steady force are preserved. Paused redraws leave the
filter unchanged while immediate display controls can still update.

`before.json` retains the paused-state failure. `after.json` checks frozen
pointer force and equal model time across different frame partitions and Steps
choices. The harness executes actual WebGPU commands under a controlled clock;
it is not a performance benchmark or a native Doe qualification. It reuses one
accepted world and resets through application controls, verifying quiet force
and reset time before each partition. `interruption.json` rechecks window-blur
release for pointer and keyboard input across the backgrounds.

An earlier harness created a new controlled-clock context for each partition.
After several contexts, initialization stopped reaching the enabled controls,
with no page failure text or GPU error. Its negative diagnostic is retained in
`cold-context-diagnostic.json`; the cause is not established. It does not qualify
ordinary cold entry. The revised partition test exercises the continuing world
whose controller is under test; ordinary hosting checks separately use native
browser time and fresh page contexts.

Reproduce with `browser/chromium/scripts/test-fawn-particle-clock.mjs` using
`FAWN_PLAYWRIGHT_MODULE`, `FAWN_CHROME_ARGS`, `FAWN_REPORT_DIR` and optional
`FAWN_TEST_ORIGIN`. Each report binds its own source and browser identity.
