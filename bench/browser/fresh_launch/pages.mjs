// Page identities retained from the archived Windows harness.
export const PAGES = {
  // Retained anchors
  compute_atomic_canvas: { url: "/harness/compute-atomic-canvas.html", kind: "compute" },
  heavy_particles: { url: "/resources/fawn-heavy-particles.html", kind: "animated" },
  image_lab: { url: "/resources/fawn-image-lab.html", kind: "imagelab" },
  magnetic_fluids: { url: "/resources/fawn-magnetic-fluids.html", kind: "animated" },
  prismatic_fluids: { url: "/resources/fawn-prismatic-fluids.html", kind: "animated" },
  cycle: { url: "/resources/fawn-cycle.html", kind: "animated" },
  // Three.js WebGPU examples (pinned r186 / 3d3a92e467aafd6749eb06f732a5f42e6d117fe4)
  three_compute_birds: { url: "/three/examples/webgpu_compute_birds.html", kind: "animated" },
  three_compute_particles: { url: "/three/examples/webgpu_compute_particles.html", kind: "animated" },
  three_lights_phong: { url: "/three/examples/webgpu_lights_phong.html", kind: "animated" },
  three_materials: { url: "/three/examples/webgpu_materials.html", kind: "animated" },
  three_custom_fog: { url: "/three/examples/webgpu_custom_fog_scattering.html", kind: "animated" },
  // Babylon.js WebGPU demos (pinned babylonjs@7.54.1)
  babylon_pbr_spheres: { url: "/harness/workloads/babylon_pbr_spheres.html", kind: "animated" },
  babylon_compute_boids: { url: "/harness/workloads/babylon_compute_boids.html", kind: "animated", expectBenchOracle: true },
  babylon_instanced_scene: { url: "/harness/workloads/babylon_instanced_scene.html", kind: "animated" },
  // webgpu-samples (pinned e040ec1a20dbbe01afed30c812e825b2639cfe5b)
  samples_compute_boids: { url: "/webgpu-samples/sample/computeBoids/index.html", kind: "animated" },
  samples_particles: { url: "/webgpu-samples/sample/particles/index.html", kind: "animated" },
  samples_shadow_mapping: { url: "/webgpu-samples/sample/shadowMapping/index.html", kind: "animated" },
  samples_deferred_rendering: { url: "/webgpu-samples/sample/deferredRendering/index.html", kind: "animated" },
  samples_image_blur: { url: "/webgpu-samples/sample/imageBlur/index.html", kind: "animated" },
  // Texture upload through a canvas
  canvas_texture_upload: { url: "/harness/workloads/canvas_texture_upload.html", kind: "animated", expectBenchOracle: true },
  // Multi-stage image filter pipeline
  image_filter_pipeline: { url: "/harness/workloads/image_filter_pipeline.html", kind: "animated", expectBenchOracle: true },
  // Doppler browser inference (gemma-3-270m-it-q4k-ehf16-af32)
  doppler_gemma270m: { url: "/harness/workloads/doppler_gemma270m.html", kind: "doppler" },
};
