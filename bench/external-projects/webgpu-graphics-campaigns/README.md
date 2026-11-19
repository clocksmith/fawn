# Third-party WebGPU graphics campaigns (Track C2)

Pins four unchanged third-party WebGPU browser graphics applications across four
independent upstream repositories and licenses so C2's Vulkan and Metal graphics
proofs do not rely on unpinned workload names or closed shader-screening flags:

1. `three_webgpu_fog_scene` (`three-fog-scattering.workload.json`):
   `https://github.com/mrdoob/three.js` at `3d3a92e467aafd6749eb06f732a5f42e6d117fe4`
   (`MIT`, `examples/webgpu_custom_fog_scattering.html`). Replaces the closed
   `three-webgpu-screen/screen.json` shader experiment that disabled the sandbox
   and Vulkan surface.
2. `image_filter_pipeline_2d` (`webgpu-samples-image-blur.workload.json`):
   `https://github.com/webgpu/webgpu-samples` at `e040ec1a20dbbe01afed30c812e825b2639cfe5b`
   (`BSD-3-Clause`, `sample/imageBlur/index.html`).
3. `particles_fluids_simulation` (`splash-mls-mpm-fluid.workload.json`):
   `https://github.com/matsuoka-601/Splash` at `3df5d621e6f153d7269037cbd8606bd83796f3fd`
   (`MIT`, `index.html`, MLS-MPM 3D fluid compute + screen-space fluid render).
4. `texture_upload_canvas_presentation` (`webgpu-utils-2d-array.workload.json`):
   `https://github.com/greggman/webgpu-utils` at `e6552a1b6797b3c88d27faa688cd8888837c9b28`
   (`MIT`, `examples/2d-array.html`, fulfilled offline with pinned
   `greggman/wgpu-matrix` at `3dba9013e3575c844c18a5bdb85981cd76c9a123`).

## Offline CPU plan & contract verification (no GPU required)

```bash
python3 bench/external-projects/webgpu-graphics-campaigns/prepare_sources.py --dry-run
node bench/external-projects/webgpu-graphics-campaigns/capture_campaign.mjs --backend amd-vulkan --plan-only
python3 -m bench.gates.webgpu_graphics_campaign_gate
```
