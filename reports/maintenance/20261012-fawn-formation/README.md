# Fawn formation across backgrounds

Component: Chromium-hosted Fawn artwork.
Intent: changed by user direction; all backgrounds offer Settings → Formation →
Free / Branded. The stored choice carries across background navigation. Domain
simulation and pause/reset behavior remain owned by each page.
Boundary effects: D4DA packages the shared formation asset; no native runtime,
compiler, model inference, or physical fluid qualification.

Branded Flow relaxes advected dye toward the logo; Wave evolves its height field
into a reflective relief; Particles converge to bounded silhouette samples.
Pointer interaction suspends guidance, and release resumes settling. Free mode
removes guidance without replacing the current numerical state. Reset preserves
the mode and pause preference. Reduced motion starts paused.

`formation.json` binds its recorded source to actual GPU buffer checks and headed
Wave/Particles desktop and emulated-mobile captures. `flow.json` separately
checks dye formation and disturbance/recovery. `lifecycle.json` checks resize,
resource failure and canvas/device limits; `controls.json` checks Flow controls.
The toolbar spacing cleanup is checked by `controls.json`; formation shader
behavior is unchanged by that CSS-only follow-up. These are browser application checks on the reported adapter, not performance
benchmarks or qualification of a custom Fawn binary. Mobile is browser emulation.

Run `browser/chromium/scripts/test-fawn-formation.mjs` and
`test-fawn-logo-flow.mjs` with `FAWN_PLAYWRIGHT_MODULE`, `FAWN_CHROME_ARGS`, and
`FAWN_HEADED=1` on a working display. `FAWN_TEST_ORIGIN` selects a hosted build;
formation checks bind served files and release identity when it is supplied.
D4DA's `tests/fawn-hosting-smoke.mjs` independently checks all hosted routes,
settings, cache retirement and preservation of local model data.

Earlier diagnostic runs caught a reserved WGSL variable name in Particles and
residual autonomous force that displaced settled particles outside the mark.
The implementation repairs the shader name and fades that force with accepted
guidance strength; the acceptance threshold was retained. Initial low-grid Wave
captures also exposed sampling steps; rendering now interpolates the evolved
surface. Retained captures show the accepted implementation.
