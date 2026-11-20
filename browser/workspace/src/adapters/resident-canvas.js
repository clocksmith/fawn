// Sent by value into a browser document. Only the adapter retains the returned
// function. Navigation discards its device, previous texture and tile hashes.
export function createResidentCanvasReader() {
  let device, previous, busy = false, disposed = false;
  const pipelines = new WeakMap();
  const shader = `
    @group(0) @binding(0) var earlier: texture_2d<f32>;
    @group(0) @binding(1) var current: texture_2d<f32>;
    @group(0) @binding(2) var<storage, read_write> dirty: array<atomic<u32>>;
    @group(0) @binding(3) var<uniform> tileSize: u32;
    @compute @workgroup_size(8, 8)
    fn compare(@builtin(global_invocation_id) id: vec3u) {
      let size = textureDimensions(current);
      if (any(id.xy >= size)) { return; }
      let p = vec2i(id.xy);
      let a = textureLoad(earlier, p, 0);
      let b = textureLoad(current, p, 0);
      // CPU hashes bind canonical unpremultiplied sRGB bytes. For translucent
      // pixels force an exact CPU check rather than trusting alpha conversion.
      if (a.a != 1.0 || b.a != 1.0 || any(a != b)) {
        let columns = (size.x + tileSize - 1u) / tileSize;
        let index = id.y / tileSize * columns + id.x / tileSize;
        atomicOr(&dirty[index / 32u], 1u << (index % 32u));
      }
    }`;
  async function prepare() {
    if (device) return;
    const adapter = await navigator.gpu?.requestAdapter();
    if (!adapter) throw Error('Canvas comparison needs a WebGPU adapter');
    const opened = await adapter.requestDevice();
    if (disposed) { opened.destroy(); throw Error('Canvas reader scope ended'); }
    device = opened;
    opened.lost.then(() => { if (device === opened) { device = null; previous = null; } });
  }
  // The selected browser surface is consumed here. A compositor-owned
  // GPUTexture can enter compareTextures directly once the browser can
  // authorize its surface, lifetime and completion fence.
  async function compareTextures(gpu, earlier, current, tileSize) {
    const { width, height } = current;
    if (earlier.width !== width || earlier.height !== height || earlier.format !== 'rgba8unorm'
      || current.format !== 'rgba8unorm' || !Number.isSafeInteger(tileSize) || tileSize < 1
      || width * height > 4 * 1024 * 1024) throw Error('Incompatible resident visual textures');
    const count = Math.ceil(width / tileSize) * Math.ceil(height / tileSize);
    if (count > 256) throw Error('Tile mask exceeds its readback budget');
    let program = pipelines.get(gpu);
    if (!program) {
      program = await gpu.createComputePipelineAsync({ layout: 'auto',
        compute: { module: gpu.createShaderModule({ code: shader }), entryPoint: 'compare' } });
      pipelines.set(gpu, program);
    }
    const size = Math.ceil(count / 32) * 4;
    let dirty, read, parameters;
    try {
      dirty = gpu.createBuffer({ size, usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC });
      read = gpu.createBuffer({ size, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      parameters = gpu.createBuffer({ size: 4, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
      gpu.queue.writeBuffer(parameters, 0, new Uint32Array([tileSize]));
      const passEncoder = gpu.createCommandEncoder();
      const pass = passEncoder.beginComputePass();
      pass.setPipeline(program);
      pass.setBindGroup(0, gpu.createBindGroup({ layout: program.getBindGroupLayout(0), entries: [
        { binding: 0, resource: earlier.createView() }, { binding: 1, resource: current.createView() },
        { binding: 2, resource: { buffer: dirty } }, { binding: 3, resource: { buffer: parameters } },
      ] }));
      pass.dispatchWorkgroups(Math.ceil(width / 8), Math.ceil(height / 8));
      pass.end();
      passEncoder.copyBufferToBuffer(dirty, 0, read, 0, size);
      gpu.queue.submit([passEncoder.finish()]);
      await read.mapAsync(GPUMapMode.READ);
      return new Uint32Array(read.getMappedRange().slice(0));
    } finally { dirty?.destroy(); read?.destroy(); parameters?.destroy(); }
  }
  async function read(region, surface, tileSize, hashTiles) {
    await prepare();
    const gpu = device;
    const { width, height } = surface;
    const base = previous?.region === region && previous.width === width && previous.height === height
      && previous.tileSize === tileSize ? previous : null;
    let texture, errorScopesOpen = true;
    gpu.pushErrorScope('out-of-memory');
    gpu.pushErrorScope('validation');
    function collectErrors() {
      errorScopesOpen = false;
      return Promise.all([gpu.popErrorScope(), gpu.popErrorScope()]);
    }
    try {
      texture = gpu.createTexture({ size: [width, height], format: 'rgba8unorm',
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT });
      gpu.queue.copyExternalImageToTexture({ source: surface },
        { texture, colorSpace: 'srgb', premultipliedAlpha: false }, [width, height]);
      const dirty = base ? await compareTextures(gpu, base.texture, texture, tileSize) : null;
      if (!dirty) await gpu.queue.onSubmittedWorkDone();
      const [validation, memory] = await collectErrors();
      if (validation || memory || gpu !== device) throw Error(validation?.message ?? memory?.message ?? 'Canvas GPU device lost');
      const result = await hashTiles(dirty, base?.hashes);
      if (gpu !== device || disposed) throw Error('Canvas reader scope ended');
      previous?.texture.destroy();
      previous = { region, width, height, tileSize, texture, hashes: result.hashes };
      texture = null;
      return { ...result, gpuMaskReadbackBytes: dirty?.byteLength ?? 0, comparedOnGpu: Boolean(dirty) };
    } finally {
      texture?.destroy();
      if (errorScopesOpen) await collectErrors();
    }
  }
  // The texture-producing browser owns authorization, device selection and the
  // producer fence. Both textures must already be ready on this device/queue.
  async function observe(...args) {
    if (busy || disposed) throw Error('Canvas reader unavailable');
    busy = true;
    try { return await read(...args); } finally { busy = false; }
  }
  function dispose() {
    disposed = true;
    previous?.texture.destroy();
    previous = null;
    device?.destroy();
    device = null;
  }
  return Object.assign(observe, { compareTextures, dispose });
}
