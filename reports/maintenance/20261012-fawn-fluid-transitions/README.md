# Fawn opens into moving fluid

Component: Chromium-hosted Fawn artwork.
Intent: changed as requested; Branded fluid entry begins as Fawn, disperses into
the simulation, and reforms at rest. Interaction and mode changes blend through
the accepted state instead of switching guidance abruptly.
Acceptance evidence: `flow.json`, `formation.json`, rendered captures and scoped
input/control reports in this directory.
Boundary effects: the shared formation helper and Flow/Wave application sources;
CanvasContext's existing hosting projection publishes them. No native runtime,
Fawn browser binary or hosting-route changes.

The first Flow buffers contain colored Fawn dye; a bounded opening current
disperses that actual state through the existing fluid solver. Wave starts with
Fawn surface relief. Its existing magnetic and bubble sources interpolate from
points inside the mark to their authored moving positions and return on recovery.
Surface reflection and tint channels relax with the height state.

The shared, versioned transition policy preserves its blend value and velocity
when input or Formation changes its target. Pause freezes progression. Stored
Free mode skips the branded entry; Reset and Count changes retain their existing
reseed behavior and preserve pause. Particles retain their existing formation
behavior and are included in the shared-helper regression.

The opening captures pause through the real control after the first submitted
frame, then resume native animation frames. Buffer readback checks recognizable
initial geometry, dispersion and reformation; the Wave check also verifies
source anchors inside the mark. Guidance history rejects abrupt per-frame blend
changes. Desktop and emulated-mobile captures show actual GPU drawing.

`flow-steep-response.json` and `wave-steep-response.json` retain the rejected
earlier response. `blocked-burst.json` retains a failed check that caught an old
uniform assignment overwriting the opening force. The corrected Flow report
checks that this force reaches the solver and that the mark visibly disperses.

Each report binds its own tested sources. The formation, response and control
reports precede removal of Flow's uniform overwrite; the final `flow.json`
qualifies that last change. Wave and the shared helper were unchanged between
those runs. Production file identity belongs to the served release manifest,
not an inferred common build identity for these separate runs.

Reproduce using `browser/chromium/scripts/test-fawn-logo-flow.mjs`,
`test-fawn-formation.mjs`, `test-fawn-pointer-interruption.mjs`,
`test-fawn-prismatic-response.mjs` and `test-fawn-prismatic-controls.mjs`.
Use `FAWN_PLAYWRIGHT_MODULE` and the required Chrome Vulkan arguments; rendered
image qualification requires `FAWN_HEADED=1` on a real display or Xvfb.

These are Chrome WebGPU artwork checks. They do not establish physical-phone
support, conserved liquid dynamics, learned diffusion, native Doe performance,
or qualification of a forced-Doe browser artifact.
