// Sent by value into a browser document. Only the adapter retains the returned
// function. Navigation discards its device, previous texture and tile hashes.
export function createResidentCanvasReader(surfaceId, gpuTileDigest) {
  let device, previous, busy = false, disposed = false;
  let adapterInfo = null, sampleNumber = 0;
  const commandLabel = surfaceId ? "blitzen-surface/" + surfaceId + "/" : "";
  const pipelines = new WeakMap(), framePipelines = new WeakMap();
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
    const info = adapter.info;
    if (info) adapterInfo = { vendor: info.vendor, architecture: info.architecture, device: info.device,
      description: info.description, isFallbackAdapter: info.isFallbackAdapter ?? adapter.isFallbackAdapter ?? null };
    opened.lost.then(() => { if (device === opened) { device = null; previous = null; } });
  }
  // The selected browser surface is consumed here. A compositor-owned
  // GPUTexture can enter compareTextures directly once the browser can
  // authorize its surface, lifetime and completion fence.
  async function compareTextures(gpu, earlier, current, tileSize, sampleLabel = '', scopeBracket = null) {
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
      const command = passEncoder.finish(sampleLabel ? { label: sampleLabel + '/diff' } : {});
      if (scopeBracket) await scopeBracket(async () => { gpu.queue.submit([command]); await gpu.queue.onSubmittedWorkDone(); });
      else gpu.queue.submit([command]);
      await read.mapAsync(GPUMapMode.READ);
      return new Uint32Array(read.getMappedRange().slice(0));
    } finally { dirty?.destroy(); read?.destroy(); parameters?.destroy(); }
  }
  async function importFrame(gpu, frame, texture, sampleLabel) {
    let pipe = framePipelines.get(gpu);
    if (!pipe) {
      const code = `
        @group(0) @binding(0) var src: texture_external;
        @group(0) @binding(1) var dest: texture_storage_2d<rgba8unorm, write>;
        @compute @workgroup_size(8, 8)
        fn main(@builtin(global_invocation_id) id: vec3u) {
          if (all(id.xy < textureDimensions(dest))) {
            textureStore(dest, id.xy, textureLoad(src, vec2i(id.xy)));
          }
        }`;
      pipe = await gpu.createComputePipelineAsync({ layout: 'auto',
        compute: { module: gpu.createShaderModule({ code }), entryPoint: 'main' } });
      framePipelines.set(gpu, pipe);
    }
    const src = gpu.importExternalTexture({ source: frame, colorSpace: 'srgb' });
    const encoder = gpu.createCommandEncoder();
    const pass = encoder.beginComputePass();
    pass.setPipeline(pipe);
    pass.setBindGroup(0, gpu.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [
      { binding: 0, resource: src }, { binding: 1, resource: texture.createView() },
    ] }));
    pass.dispatchWorkgroups(Math.ceil(texture.width / 8), Math.ceil(texture.height / 8));
    pass.end();
    gpu.queue.submit([encoder.finish(sampleLabel ? { label: sampleLabel + '/import' } : {})]);
  }
  async function read(region, surface, tileSize, hashTiles, width, height, fullRead = false, frameTransport = null, scopeBracket = null) {
    await prepare();
    const gpu = device;
    const sampleLabel = commandLabel ? commandLabel + 'sample-' + (++sampleNumber) : '';
    const base = !fullRead && previous?.region === region && previous.width === width && previous.height === height
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
        usage: GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC | GPUTextureUsage.TEXTURE_BINDING
          | GPUTextureUsage.RENDER_ATTACHMENT | (frameTransport === 'external-texture' ? GPUTextureUsage.STORAGE_BINDING : 0) });
      if (frameTransport === 'rgba-upload') {
        const rect = surface.visibleRect;
        if (rect.width !== width || rect.height !== height || surface.rotation || surface.flip) {
          throw Error('Explicit RGBA upload requires an unrotated frame at native scale');
        }
        const bytes = new Uint8Array(width * height * 4);
        await surface.copyTo(bytes, { rect, format: 'RGBA', colorSpace: 'srgb', layout: [{ offset: 0, stride: width * 4 }] });
        gpu.queue.writeTexture({ texture }, bytes, { bytesPerRow: width * 4 }, [width, height]);
      } else if (frameTransport === 'external-texture') await importFrame(gpu, surface, texture, sampleLabel);
      else gpu.queue.copyExternalImageToTexture({ source: surface },
        { texture, colorSpace: 'srgb', premultipliedAlpha: false }, [width, height]);
      const dirty = base ? await compareTextures(gpu, base.texture, texture, tileSize, sampleLabel, scopeBracket) : null;
      if (!dirty) await gpu.queue.onSubmittedWorkDone();
      const result = await hashTiles(dirty, base?.hashes, gpu, texture);
      const [validation, memory] = await collectErrors();
      if (validation || memory || gpu !== device) throw Error(validation?.message ?? memory?.message ?? 'Canvas GPU device lost');
      if (gpu !== device || disposed) throw Error('Canvas reader scope ended');
      previous?.texture.destroy();
      previous = { region, width, height, tileSize, texture, hashes: result.hashes };
      texture = null;
      return { ...result, ...(sampleLabel && dirty ? { diffCommandLabel: sampleLabel + '/diff' } : {}),
        frameHostCopyBytes: frameTransport === 'rgba-upload' ? width * height * 4 : 0, externalTextureCopyBytes: width * height * 4, gpuMaskReadbackBytes: dirty?.byteLength ?? 0, comparedOnGpu: Boolean(dirty) };
    } finally {
      // The compositor releases its VideoFrame immediately upon return. Even a
      // later validation failure must wait for work already submitted with it.
      if (frameTransport === 'external-texture') await gpu.queue.onSubmittedWorkDone().catch(() => {});
      texture?.destroy();
      if (errorScopesOpen) await collectErrors().catch(() => {});
    }
  }
  // The texture-producing browser owns authorization, device selection and the
  // producer fence. Both textures must already be ready on this device/queue.
  async function lockedRead(...args) {
    if (busy || disposed) throw Error('Canvas reader unavailable');
    busy = true;
    try { return await read(...args); } finally { busy = false; }
  }
  function observe(region, surface, tileSize, hashTiles) {
    return lockedRead(region, surface, tileSize, hashTiles, surface.width, surface.height);
  }
  // A VideoFrame returned by Chromium Element Capture is already restricted by
  // the browser. Its owner keeps it alive until the GPU transfer has completed.
  async function readFrame(region, frame, fullRead, transport, scopeBracket = null) {
    const width = frame.displayWidth, height = frame.displayHeight;
    if (!width || !height || width * height > 4 * 1024 * 1024) throw Error('Capture exceeds pixel budget');
    let tileSize = 64;
    while (Math.ceil(width / tileSize) * Math.ceil(height / tileSize) > 256) tileSize *= 2;
    async function hashTexture(dirty, previousHashes, gpu, texture) {
      const copies = [];
      const hashes = previousHashes ? [...previousHashes] : [];
      let bufferSize = 0, mappedReadbackBytes = 0, tilePixelReadbackBytes = 0, index = 0;
      for (let y = 0; y < height; y += tileSize) {
        for (let x = 0; x < width; x += tileSize, index++) {
          if (dirty && !(dirty[index >>> 5] & (1 << (index % 32)))) continue;
          const w = Math.min(tileSize, width - x), h = Math.min(tileSize, height - y);
          const stride = Math.ceil(w * 4 / 256) * 256;
          copies.push({ x, y, w, h, stride, offset: bufferSize, index });
          tilePixelReadbackBytes += w * h * 4;
          bufferSize += stride * h;
        }
      }
      if (gpuTileDigest) return gpuTileDigest(gpu, texture, copies, hashes);
      if (bufferSize > 32 * 1024 * 1024) throw Error('Padded tile readback exceeds 32 MiB');
      if (bufferSize) {
        const buffer = gpu.createBuffer({ size: bufferSize, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
        try {
          const encoder = gpu.createCommandEncoder();
          for (const t of copies) encoder.copyTextureToBuffer({ texture, origin: [t.x, t.y] },
            { buffer, offset: t.offset, bytesPerRow: t.stride }, [t.w, t.h]);
          gpu.queue.submit([encoder.finish()]);
          await buffer.mapAsync(GPUMapMode.READ);
          const bytes = new Uint8Array(buffer.getMappedRange());
          mappedReadbackBytes = bytes.byteLength;
          await Promise.all(copies.map(async t => {
            const packed = new Uint8Array(t.w * t.h * 4);
            for (let row = 0; row < t.h; row++) {
              const start = t.offset + row * t.stride;
              packed.set(bytes.subarray(start, start + t.w * 4), row * t.w * 4);
            }
            const hash = await crypto.subtle.digest('SHA-256', packed);
            hashes[t.index] = Array.from(new Uint8Array(hash), b => b.toString(16).padStart(2, '0')).join('');
          }));
        } finally { buffer.destroy(); }
      }
      return { hashes, cpuPixelReadBytes: 0, tilePixelReadbackBytes, mappedReadbackBytes, rehashedTiles: copies.length };
    }
    const result = await lockedRead(region, frame, tileSize, hashTexture, width, height, fullRead, transport, scopeBracket);
    return { width, height, tileSize, ...result };
  }
  function dispose() {
    disposed = true;
    previous?.texture.destroy();
    previous = null;
    device?.destroy();
    device = null;
  }
  return Object.assign(observe, { compareTextures, readFrame, dispose, adapterInfo: () => device ? adapterInfo : null });
}
