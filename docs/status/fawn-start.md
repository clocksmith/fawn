# Fawn hosted demonstrations

The hosted background cycles Flows, Magnets, and Particles on one canvas. Each
scene holds with its own simulation, shading, and pointer response. A scene change
hands the outgoing state to the incoming scene and to one carried population that
moves it under both scenes' motion while the three images blend. Logo formation
carries the live image into the mark as moving material: each region travels its
own curved path to its own place in the mark while the scene keeps simulating, and
release reverses the same paths.

[`fawn-scenes.js`](../../browser/chromium/resources/fawn-scenes.js) is the single
orchestrator. Each scene module owns its shaders; [`fawn-gpu.js`](../../browser/chromium/resources/fawn-gpu.js)
holds the shared frame uniform and pipeline helpers, and [`fawn-mark.js`](../../browser/chromium/resources/fawn-mark.js)
owns formation for every scene. The retained
standalone benchmark pages are separate workloads, not shader sources.

Checks are local visual and lifecycle checks in a WebGPU browser; no artwork test
suite is retained. Physical-phone, Safari, native Doe and performance qualification
remain separate. The [20261013 publication receipt](../../reports/releases/fawn/20261013-publication.json)
records the previous hosted identity. [Workspace status](fawn-workspace.md) covers
the prospective durable-work service.

Doe owns the source under `browser/chromium/resources/`; sibling D4DA owns the
isolated package through `firebase.fawn.json`. The canonical surface is
[CanvasContext](https://canvascontext.com/); its [live manifest](https://canvascontext.com/release.json)
identifies the published source. Local edits do not update it until deployed.
Public aliases open the same unified canvas, with Flow first and no selector.

History: [the preceding living-material status](archive/20261013-fawn-living-material.md).
