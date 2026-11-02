# Fawn hosted demonstrations

The hosted background cycles Flows, Magnets, and Particles on one canvas. One
population of parcels carries position, velocity, color, and material through every
scene and transition, and gathers into the mark while the scene's field keeps moving
it. Release retraces each parcel toward its place in the even starting spread, so
material floats back across the scene instead of staying clumped. Unformed Flows and
Magnets shade their live fields directly; parcels take over as the mark gathers.
Each scene shows its full field before the mark gathers late in the hold. A press keeps
each scene's response through every blend: Magnets attract, Particles repel, a held
press swirls where the two cancel, and material under it leaves its formation and
return springs until release. Material spirals into the mark on the live field rather than
taking straight lines. Magnetic material is drawn into the magnets and draws only where it is
continuous and the field holds it; the forming mark becomes one film. The hand acts on
nearby material faster than the fields do, and a held drag sweeps it along. Fluid relief carries pixel glitter. Particles follow the standalone
particle page's galaxy with fading trails and form the mark in its positional palette.

[`fawn-scenes.js`](../../browser/chromium/resources/fawn-scenes.js) owns cadence,
[`fawn-cycle-clock.js`](../../browser/chromium/resources/fawn-cycle-clock.js) owns
weights/easing, and the [source map](../../browser/chromium/resources/README.md)
routes solver, input, resource and renderer changes. The unified app directly
imports readable shader modules. Active historical workload fixtures belong to
[`bench/browser/fixtures/fawn/`](../../bench/browser/fixtures/fawn/README.md), not
the hosted product. No generated shader bundle or standalone product pages remain.

Checks are local visual and lifecycle checks in a WebGPU browser; focused clock/input contracts are retained. Physical-phone, Safari, native Doe and performance qualification
remain separate. The [20261013 publication receipt](../../reports/releases/fawn/20261013-publication.json)
records an earlier hosted identity. [Workspace status](fawn-workspace.md) covers the
durable-work service.

Doe owns the source under `browser/chromium/resources/`; sibling D4DA owns the
isolated package through `firebase.fawn.json`. The canonical surface is
[CanvasContext](https://canvascontext.com/); its [live manifest](https://canvascontext.com/release.json)
identifies the published source. Local edits do not update it until deployed.
Public aliases open the same unified canvas, with Flow first and no selector.

History: [the preceding living-material status](https://github.com/doerun/doe/blob/cf89bce7b9ed096c7d406cffe62dfa9e386e7902/docs/status/archive/20261013-fawn-living-material.md).
