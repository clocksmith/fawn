# CanvasContext artwork

Start at `fawn-cycle.html`. The homepage uses one canvas and one resident parcel
population; scenes change the forces and appearance of that material.

| Change | Owner |
| --- | --- |
| Scene order, labels, hold time and outgoing blend duration | `fawn-scenes.js` |
| Quintic smootherstep, named scene weights, visible-time clock and handoff events | `fawn-cycle-clock.js` |
| Pointer stroke accumulation, charge/release and one-time frame consumption | `fawn-cycle-interaction.js` |
| Scene field passes and arrival preparation | `fawn-scene-passes.js` |
| Buffer lifetime, bindings, uniform packing and parcel dispatch | `fawn-cycle-gpu.js` |
| Parcel forces, retained position/velocity/pigment, gather and release | `fawn-parcels.js` |
| Shared surface/point footprints, relief, lighting and trails | `fawn-parcel-renderer.js` |
| Mark geometry, boundary field and formation response | `fawn-formation.js` |
| Frame submission, page lifecycle, controls and completed-frame meter | `fawn-cycle.js` |
| GPU transfer kernels | `fawn-cycle-transfers.js` |

The sequence is Flows (8-second hold, 6-second blend), Magnets (8 + 4), then
Particles (8 + 4): 38 seconds of visible time. The clock returns one set of named
weights used by input, parcel forces and rendering. Only uniform packing turns
those into WGSL `theme.xyz` in flow / wave / particles order. `wave` is the stable
internal identifier for the scene labelled Magnets.

At blend start, `prepareScene(encoder, destination)` runs once. Entering Magnets
seeds the wave surface from the live flow. Entering Particles copies nothing.
Entering Flows deposits parcel pigment and momentum and clears pressure/curl.
Both neighboring field solvers run during the blend. Parcels advance once after
the solvers, then the shared renderer draws them; no handoff resets parcels.

## Shader editing and packaging

Edit `fawn-flow-shaders.js` and `fawn-wave-shaders.js` directly. The app imports
these readable modules; there is no generated shader bundle or source projection.

```sh
node browser/chromium/tests/test-fawn-cycle.mjs
python3 browser/chromium/scripts/package-fawn-resources.py --output /tmp/fawn-native
```

The native packager embeds the real module graph through an import map for
`file:` launch, without relaxing browser file-access protections. Its generated
HTML belongs only in build output. Hosted CanvasContext loads the direct modules;
D4DA packages one index page, and Firebase rewrites the existing legacy URLs to it.

The five old standalone workloads live only under `bench/browser/fixtures/fawn/`
because maintained benchmark consumers use them. Fluid fixtures import these same
solver modules and keep their distinct benchmark rendering/interaction code.
`formation-material.js` there owns the old transported-sheet helpers. Historical
receipts retain their original source paths; new runs bind the moved fixture and
its shader dependencies. Benchmark fixtures are excluded from product packaging.
