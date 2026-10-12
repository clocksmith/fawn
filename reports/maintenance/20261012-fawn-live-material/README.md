# Fawn settles from the active fluid surface

Component: hosted Flow and Wave artwork.
Intent: changed as requested. Form the mark from the active fluid surface and
restore Flow's curl-driven glitter/prisms. Particles is unchanged.
Acceptance evidence: [live-material.json](live-material.json),
[responsive.json](responsive.json), and the source-bound runners below.
Boundary effects: the shared formation helper and two HTML/WGSL applications;
no native runtime, Fawn browser binary, hosting-route or model changes.

Flow and Wave no longer upload a seeded opening logo or drive their field toward
newly generated logo dye/height. The renderer transports a continuous sheet of
the current GPU material toward the mark's bounds. Its final silhouette constrains
that sheet; the existing color, curls, bubbles and surface height remain its
material. Wave's silhouette curvature uses the same reflection model as its
existing film. This is a surface deformation and silhouette constraint, not
particle-level transport or a mass-conserving fluid simulation.

The blend ramps from accepted state, including when interrupted. Simulation time
slows as the field settles; fully settled fields stop computing rather than
reseed. Pointer coordinates and Flow stroke displacement map back into the
transported material. Free/Branded changes preserve buffers. Explicit Reset or
Count changes still rebuild their initial conditions, and viewport changes update
geometry without reseeding the fluid. Both modes begin from the same live fields.

Flow restores the pre-logo curl palette and pigment-independent metallic/prism
lighting, with interpolated dye, velocity, curl and relief, mild local contrast,
a raised background and bounded exposure. The renderer reuses corner samples.
It retains one four-vertex sheet, avoiding the rejected cell-scatter mapping's
scanline seams and per-cell drawing.

The GPU projection check uses each production vertex shader with an independent
material marker and checks its interpolated screen position. Ordinary desktop
and emulated-phone journeys read the actually rendered fluid buffers: Formation
preserves the current field before its first transition frame, settlement creates
no material uploads, different stirred histories retain different settled fields,
and rest, interruption and pause preserve their specified state. Responsive,
canvas lifecycle and circular projection checks also pass. The circle probe now
subtracts the same zero-field backdrop before applying its original contrast
threshold, so colorful lighting is not mistaken for the injected geometry.

```sh
node browser/chromium/scripts/test-fawn-live-material.js
node browser/chromium/scripts/test-fawn-responsive.js
node browser/chromium/scripts/test-fawn-square-world.mjs
node browser/chromium/scripts/test-fawn-canvas-lifecycle.mjs
```

Set `FAWN_PLAYWRIGHT_MODULE` to an installed Playwright ESM module. On macOS set
`FAWN_CHROME_ARGS='["--enable-unsafe-webgpu"]'` for the latter two runners.
Screenshots stay outside Git. The earlier dye/height-mask and seeded-opening
receipts retain their preceding-source meaning; they do not qualify this surface
transport. These are Chrome WebGPU artwork checks, not physical-phone support,
native Doe performance or forced-Doe browser qualification. Publication remains
identified by CanvasContext's release manifest.
