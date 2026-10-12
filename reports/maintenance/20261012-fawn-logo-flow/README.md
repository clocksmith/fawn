# Fawn logo flow

The Flow demo starts with seeded pigment noise. Canonical Fawn logo paths supply
a signed-distance field that guides velocity and dye relaxation. Local diffusion
and advection preserve disturbances; release lets the shape reform. Logo disables
guidance, and Reset starts another seeded run while preserving pause.

`report.json` binds the HTML and guidance source to actual GPU buffer readback
and desktop/mobile captures. `controls.json` checks pause, reset, settings, resize,
reduced motion and failure presentation; `lifecycle.json` checks canvas/resource
failures and existing background pages. Run the scripts under
`browser/chromium/scripts/` with `FAWN_PLAYWRIGHT_MODULE` pointing at an installed
Playwright module. Select browser flags through `FAWN_CHROME_ARGS`. Formation
captures use `FAWN_HEADED=1` with a working display.

Earlier headless captures were black despite valid dye readback. Final visual
evidence uses headed Chrome, native animation frames and the reported Intel
adapter. The initial shader used a reserved keyword, then was repaired; the
first local browser launch lost its device on both unchanged and edited Flow.
Explicit Vulkan launch flags repaired the test environment. None of these
application checks qualify the Fawn browser, native Doe, a learned diffusion
model, liquid mass conservation or a performance advantage.

D4DA packages the additional guidance script through its Fawn allowlist.
Hosting identity and live checks remain separate from local application proof.
