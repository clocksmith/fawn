# Fawn hosted demonstrations

Flow and Wave open as Fawn in Branded mode, disperse, and reform at rest.
Guidance and Wave's highlights blend through accepted state during interaction
and mode changes. [Fluid-transition evidence](../../reports/maintenance/20261012-fawn-fluid-transitions/README.md)
binds the tested sources to GPU state and rendered desktop/mobile captures.
Free mode skips the opening logo; Reset preserves pause and reseeds the existing
initial conditions. The live release manifest below identifies deployed source.

[Particle reset evidence](../../reports/maintenance/20261012-fawn-particle-reset/README.md)
retains the preceding-source gesture leak and corrected reset behavior. Reset
clears particle input alongside positions and time while preserving pause and
settings. This is Chrome WebGPU application evidence; the live release manifest
below identifies the deployed source.

[The original Flow formation evidence](../../reports/maintenance/20261012-fawn-logo-flow/README.md)
retains the earlier noise-to-logo experiment. These application checks do not
qualify native Doe or the Fawn browser binary.

[Canvas and failure regressions](../../reports/maintenance/20261011-fawn-canvas-lifecycle/README.md)
retain the default-canvas initialization repair, aspect-preserving device-limit
scaling, and Chroma terminal resource cleanup. The report separates reproduced
preceding-source failures, harness corrections, adapter acquisition failures,
and passing application checks. This is not a hosting publication.

The canonical demo is [CanvasContext](https://canvascontext.com/). Doe owns the
HTML/WGSL under `browser/chromium/resources/`; the sibling D4DA repository owns
packaging and publication through `firebase.fawn.json`. Legacy D4DA Doe routes
redirect to CanvasContext with their demo filenames intact.
The [live release manifest](https://canvascontext.com/release.json) identifies
the published demo source and hosting package.

The current hosting package opens Flow, while Particles remains available through
the background switcher and its named page. Local changes do not update the live
site until deployed. The browser launcher has its own local start page; existing
installations can retain an older packaged copy. The preview visibly links to Fawn's
Chromium integration on GitHub and identifies the visitor's browser WebGPU
backend in Settings. The preview has no search/address field.

The main page is the public demo entry point. Settings and the README no longer
send visitors to standalone demos. Particle Trails, Magnetic Fluids, Prismatic
Fluids, and Image Lab remain in `browser/chromium/resources/` for benchmarks and
diagnostics. Magnetic Fluids and Prismatic Fluids remain public fixtures;
Particle Trails and Image Lab are repository-only. Lifecycle probes navigate
directly to the fluid fixture rather
than requiring a public navigation link.

The three hosted pages have a compact background disclosure labelled with the
active demo and no instruction subtitle block. Its trigger and dropdown share
their width and text alignment;
rows retain touch targets and a current-selection checkmark.
It switches between Flow (Prismatic Fluids), Wave (Magnetic Fluids),
and Particles (the particle cycle). [Menu diagnostics](../../reports/maintenance/20261011-fawn-background-menu/README.md)
retain desktop and emulated mobile layout, naming, keyboard and navigation checks.
Particle Trails and Image Lab are
excluded from the menu and hosting package, and retained as repository-only
benchmarks. The
main page continues cycling its own particle fields; choosing another simulator
navigates to that separate implementation. Escape restores focus and outside
interaction closes the disclosure.
Flow uses a narrower hover radius than its pressed-pointer radius, declared by
`HOVER_RADIUS` and `POINTER_RADIUS` in the prismatic-fluid source.
Wave's moving fields and bubbles use the `WAVE_ACTIVITY` configuration in its
source, with source positions prepared once per frame for the surface shader.
Particles use bounded alpha compositing and edge-faded trail history to prevent
bright rims around dense clouds and the viewport.
The visible footer contains FPS, its sparkline, and a GitHub link. Workload and
GPU details appear in Settings; operational status remains available to assistive
technology and failures retain their visible alert.

The benchmark pages align Particle Trails, Magnetic
Fluids, Prismatic Fluids, and Image Lab with the main page's fullscreen canvas,
Fawn branding, Settings panel, GitHub link, and compact footer. It has no address
field. The packaged simulators are published on CanvasContext; Particle Trails
and Image Lab remain repository-only. That alignment preserved simulation shaders, default workloads and image-processing
resolution; the later Flow logo experiment has its own changed-source evidence.
Pause freezes simulation and shader time; reset and resize can redraw while
paused. Image Lab renders on demand and reports completed-update latency.

[Alignment application evidence](../../reports/maintenance/20261005-fawn-demo-pages/README.md)
binds the preceding alignment source to matched GPU state and pixels, allocation/upload counts,
desktop/mobile controls, pause/reset/resize, image upload recovery, and device
lifecycle checks. [The standalone-page check](../../browser/chromium/scripts/test-fawn-demo-pages.mjs)
runs with `FAWN_PLAYWRIGHT_MODULE` pointing to an installed Playwright ESM module;
artifacts go under `bench/out/fawn-demo-pages/` by default. The
[prismatic-specific UI check](../../browser/chromium/scripts/test-fawn-prismatic-controls.mjs)
remains available for narrower validation.

[Switcher and clock evidence](../../reports/maintenance/20261005-fawn-demo-switcher/README.md)
binds the switcher-introduction source to navigation, desktop/mobile layout, keyboard dismissal,
and a real particle cycle. RAF timestamps earlier than initialization now produce
zero elapsed time instead of negative simulation steps or a negative field phase.
These demos have no established Doe-versus-Dawn application speed comparison;
local diagnostics use Chrome's existing WebGPU provider. The current menu is
checked by the switcher script and references the existing packaged simulators.

The main particle page keeps simulated positions in normalized viewport
coordinates. Its circulating flow and reflective, inward-guided edges prevent
particles from collapsing into an offscreen corner. Canvas size observation
updates drawing targets across viewport changes without reseeding particles.
[Mobile regression evidence](../../reports/maintenance/20261011-fawn-particle-mobile/README.md)
retains the preceding-source failure, GPU state and pixel readback, repeated
cycles, rotation, and input checks. The [viewport regression](../../browser/chromium/scripts/test-fawn-particle-mobile.mjs)
and [independent field oracle](../../browser/chromium/scripts/test-fawn-particle-fields.mjs)
exercise the corrected application. These checks use Chrome WebGPU with
emulated mobile viewports; physical phone and forced-Doe browser qualification
remain separate.

The landing page retains particle settings, continuous field transitions,
pointer and keyboard interaction, pause, reset, and reduced-motion
startup. The particle renderer uses triangle strips and draws fading trails and
particles within the same render pass. Simulation dispatches share a compute
pass while preserving their order. The prismatic solver stores scalar pressure.
All hosted demos bound pending frames, stop scheduling while hidden, and sample
completed-frame intervals separately from clamped simulation time.

Magnetic Fluids uses a critically damped response for magnet position and
strength across hover, press, drag, release and cancellation. Mouse release
returns to hover strength; touch release removes the magnet. Pause freezes this
response with the surface. [Transition diagnostics](../../reports/maintenance/20261011-fawn-magnetic-transition/README.md)
bind the input response to an independent ODE reference, GPU fluid state and
desktop/emulated mobile controls.

The magnetic surface has a black body with spectral color concentrated in curved
reflection bands. Surface-coordinate gradients preserve the appearance across
simulation grid sizes; normalized lighting avoids a bright wash over flat areas.
[Material diagnostics](../../reports/maintenance/20261011-fawn-magnetic-material/README.md)
retain matched-field GPU pixel comparisons and the existing interaction checks.
The fluid simulation and magnet response are unchanged by this material revision.

Prismatic Fluids consumes pointer displacement once as an immediate stroke
impulse. Velocity and dye cover the segment between sampled pointer positions;
presses add a stronger impulse and color burst, including taps between frames.
Input history no longer decays into later frames, and only one rendering frame
may remain in flight. [Response diagnostics](../../reports/maintenance/20261011-fawn-prismatic-response/README.md)
retain preceding-source comparisons, first-frame GPU fluid state, mouse/touch
release and cancellation, and the existing control checks.

[Retained application evidence](../../reports/maintenance/20261005-fawn-demo-optimization/README.md)
binds the source HTML to physical Chrome GPU state comparisons, screenshot
identity, field-transition checks, controls, lifecycle checks, and alternating
before/after observations. These are application diagnostics on the visitor's
browser WebGPU backend. They do not qualify a forced-Doe Chromium release or
establish a portable optimality claim. Tail variation remains in the raw report.

History: [the earlier start-page preview](archive/20261005-fawn-start-preview.md).
