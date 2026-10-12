# Square worlds and particle motion

Component: Chromium-hosted Fawn artwork.
Intent: changed by user direction; fill the viewport without axis stretching,
show individual moving particles, and give every background Count and Steps.
Boundary effects: D4DA checks and publishes this source through its existing
CanvasContext projection. Native Doe and physical fluid qualification are separate.

A shared square-world camera crops to the viewport instead of independently
scaling the axes. Pointer mapping, logo targets, cells and point sprites use that
same world scale. Resize changes the view and target field; the accepted workload
count stays fixed. Fluid counts mean cells; Particles counts moving points. The
readout names the unit. Shared count/step choices respect the actual GPU limits.
Particles opens with a sparse cloud, round bright points, short trails, stable
jitter and bounded continuous wandering within the mark.

Steps means complete model steps per frame. Each step receives its share of the
frame timestep; Flow pressure solves remain an internal fixed policy. Press and
drag impulses are divided across those steps without repeating their full force.
The CPU uniform allocation follows the shader structure's actual byte length.

Formation policy v2 uses a square-world target raster. The new workload policy
owns common choices and authored defaults. Flow's uniform adds a viewport vector;
Particles adds a viewport vector and vec4 target records containing world XY,
inward distance and a reserved float. These are private browser-application
contracts compiled together with their shaders, not native package ABI changes.

`projection.json` measures actual pixels from circular numerical states on wide,
portrait and square screens. It checks both equal axes and expected diameter,
actual buffer count, labels and dispatched work at different Steps values.
`formation.json` and `flow.json` retain source-bound numerical formation/recovery
and headed captures. `response.json` checks immediate mouse/touch impulses and
cancellation, waiting for the actual DOM touch event before sampling GPU work.
`controls.json` and `lifecycle.json` cover pause/reset, settings, resize, reduced
motion, resource failure and canvas limits. Each report names its tested source;
later formatting and control-state changes do not inherit a different file hash.

The default mobile capture is uninstrumented and bound separately in
`default-capture.json`; one earlier instrumented image omitted painted chrome
despite visible DOM controls. It is not used to qualify interface pixels.

All evidence is browser application evidence on the recorded adapter, with
emulated mobile. There is no native Fawn binary, model inference, measured fluid
calibration or performance comparison claim. The earlier agreement metric for
Wave included empty background; its current oracle uses foreground height IoU.
The circle oracle bounds its analysis region and checks expected size to exclude
chrome from the measurement. Neither oracle accepts a rendered logo overlay as
proof of model state.
