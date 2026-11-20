# Browser canvas/WebGPU fusion contract

Status: `draft`

## Objective

Define the probe artifact for visible canvas 2D, WebGPU, image-filter, and
presentation graph sharing when Chromium exposes stable command boundaries.
Layout, accessibility, permissions, and origin policy remain browser-owned.

## Input shape

The fusion probe schema is
[`config/browser-canvas-webgpu-fusion.schema.json`](../../../config/browser-canvas-webgpu-fusion.schema.json).
The probe records:

- runtime identity path
- participating surfaces
- responsibility-map entries
- visible graph nodes and edges
- output hashes
- timing scopes
- fallback reasons
- origin-scoped privacy policy

When `--runtime-identity-root` is supplied, the checker resolves
`runtimeIdentity.runtimeIdentityPath` and requires its selected runtime and
fallback state to match either a `browser_runtime_identity` artifact or the
referenced browser smoke report.

## Output artifacts

The artifact kind is `browser_canvas_webgpu_fusion_probe`. It is diagnostic
until a browser-lane probe can produce the same shape from forced-Doe Chromium
execution. The smoke builder derives the current graph, output hash, timing
scopes, and fallback reasons from Playwright smoke output for the selected
runtime mode.

## Failure taxonomy

The structural checker reports:

- `missing_surface_kind`
- `invalid_schema_version`
- `invalid_artifact_kind`
- `duplicate_surface_id`
- `duplicate_node_id`
- `missing_surface_reference`
- `missing_node_reference`
- `missing_output_hash`
- `missing_timing_scope`
- `unsafe_privacy_policy`
- runtime identity reference failures from the shared browser runtime-identity
  reference checker

## Fallback policy

Fallback reasons are explicit per surface. A probe with fallback reasons remains
diagnostic unless browser claim policy accepts the fallback class.

## Gate coverage

- Schema validation: `python3 bench/gates/schema_gate.py`
- Builder:
  `python3 browser/chromium/scripts/build-browser-canvas-webgpu-fusion.py --report <browser-smoke.json> --mode doe --out <canvas-webgpu-fusion.json>`
- Smoke runner hook:
  `./browser/chromium/scripts/run-smoke.sh --mode doe --canvas-webgpu-fusion-out <canvas-webgpu-fusion.json> --canvas-webgpu-fusion-mode doe`
- Structural check:
  `python3 browser/chromium/scripts/check-browser-canvas-webgpu-fusion.py --probe examples/browser-canvas-webgpu-fusion.sample.json --runtime-identity-root .`

## Promotion criteria

Promotion requires a generated Chromium artifact with the same graph shape,
stable output hashes, declared timing scopes, and no hidden fallback.

## Resident visual evidence

Blitzen's optional WebGPU reader accepts two ready RGBA8 textures on the same
device and returns a tile bitmask of at most 32 bytes. In stock Chrome the
adapter copies each authorized canvas into a renderer-owned texture.
The optional compositor input uses browser-mediated Element Capture in an
isolated scripting world. After the browser acknowledges a self-tab restriction,
Blitzen holds a compositor-produced VideoFrame through GPU submission and
completion. Its external-texture transport imports the frame and writes a
canonical RGBA8 texture. An explicitly selected WebCodecs upload control copies
RGBA through host memory. The receipt identifies the transport. Verify actual
shared-image backing, fences, copies, and selected backend on the target Fawn
build before claiming zero-copy execution. In the pinned Chromium patch,
`AssociateMailboxForDoe` may use a native shared image or select Skia staging.
The backend's device fence and the final GPU queue completion serve different
roles. A successful JavaScript API call cannot establish which native path ran.
[Chromium frame-sink capture](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/services/viz/privileged/mojom/compositing/frame_sink_video_capture.mojom)
and [Blink external-texture lifetime](https://chromium.googlesource.com/chromium/src/+/4a6fb38567da21dc86f8ba468ac0ca9002f4f606/third_party/blink/renderer/modules/webgpu/gpu_external_texture.cc)
describe the two boundaries at the pinned revision.

Compare stock Chromium/Dawn, stock with a native helper, Fawn/Dawn and forced
Fawn/Doe on the same permitted observations, planner, actions and journal.
Record change-to-verified-outcome p50/p95, correct and missed changes, duplicate
effects, peak RSS/GPU memory, copies and readbacks, planner calls and measured
model spend. Missing model usage stays unknown. Separate calls or CPU bytes
requested through browser APIs from driver-level transfers. Record whether the
start clock came from compositor commit or the fixture's request to repaint.
