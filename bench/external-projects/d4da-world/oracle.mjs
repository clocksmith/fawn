// Independent CPU expectations for a planar fixture rendered by D4DA's real material.
import assert from 'node:assert/strict';

export async function captureTerrainOracle(plan) {
  await import('/js/simulatte-world-backends.js');
  const factory = window.SimulatteWorldRendererFactory;
  const { size, canvasSize, gridStep, origin } = plan;
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = canvasSize;
  document.body.append(canvas);
  const stride = factory.meshVertexStride;
  const vertices = new Float32Array((size + 1) ** 2 * stride), indices = [];
  for (let x = 0; x <= size; x++) for (let z = 0; z <= size; z++) {
    vertices.set([2 * x / size - 1, 2 * z / size - 1, 0.5, 0, 0, 1,
      origin + x * gridStep, origin + z * gridStep, 0, 1, 0], (x * (size + 1) + z) * stride);
    if (x < size && z < size) {
      const a = x * (size + 1) + z, b = (x + 1) * (size + 1) + z;
      indices.push(a, b, b + 1, a, b + 1, a + 1);
    }
  }
  const palette = window.D4DARouteConfig.rainbowTilePalette;
  const colors = new Float32Array(factory.paletteSize * 4);
  palette.forEach((color, index) => colors.set([...color, 1], (factory.tilePaletteOffset + index) * 4));
  const renderer = await factory.createSceneRenderer({ canvas,
    geometry: { vertexCapacity: vertices.length, triangleIndices: new Uint16Array(indices),
      lineIndices: new Uint16Array() }, preferredCapability: 'webgpu' });
  try {
    if (renderer.capability !== 'webgpu') throw new Error('Terrain oracle fell back');
    const device = renderer.renderer.backend.device;
    const bytesPerRow = Math.ceil(canvasSize * 4 / 256) * 256;
    const frames = [];
    for (const timeSec of [0, 11]) {
      await new Promise(requestAnimationFrame);
      renderer.draw({ clearColor: [0, 0, 0], mesh: { vertexData: vertices,
        heightMin: -1, heightMax: 1, tint: [1, 1, 1], lineTint: [0, 0, 0],
        tintStrength: 0, paletteStrength: 0, paletteCount: 0, paletteColors: colors,
        roadBlend: 0, tileColorBlend: 1, gridOrigin: origin, gridStep,
        crystalBlend: 0, viewDirection: [0, 1, 0], glowCenter: [0, 0],
        glowStrength: 0, tileAlpha: 1, lineAlpha: 0, timeSec } });
      // Copy the actual canvas texture in the rendering task, before presentation.
      const texture = canvas.getContext('webgpu').getCurrentTexture();
      const readback = device.createBuffer({ size: bytesPerRow * canvasSize,
        usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      const encoder = device.createCommandEncoder();
      encoder.copyTextureToBuffer({ texture }, { buffer: readback, bytesPerRow },
        { width: canvasSize, height: canvasSize });
      device.queue.submit([encoder.finish()]);
      await readback.mapAsync(GPUMapMode.READ);
      const data = new Uint8Array(readback.getMappedRange());
      const pixels = [];
      for (let z = 0; z < size; z++) for (let x = 0; x < size; x++) {
        for (const [dx, dz] of [[0.25, 0.25], [0.75, 0.25], [0.25, 0.75], [0.75, 0.75]]) {
          const offset = Math.floor(canvasSize - 1 - (z + dz) * canvasSize / size) * bytesPerRow
            + Math.floor((x + dx) * canvasSize / size) * 4;
          const rgba = [...data.slice(offset, offset + 4)];
          if (texture.format === 'bgra8unorm') [rgba[0], rgba[2]] = [rgba[2], rgba[0]];
          pixels.push({ x, z, rgba });
        }
      }
      frames.push({ timeSec, pixels, format: texture.format });
      readback.unmap(); readback.destroy();
    }
    await renderer.renderer.backend.device.queue.onSubmittedWorkDone();
    return { capability: renderer.capability, palette, frames };
  } finally { await renderer.dispose(); canvas.remove(); }
}

export function validateTerrainOracle(result, plan) {
  assert.equal(result.capability, 'webgpu');
  assert.equal(result.palette.length, 8);
  assert.equal(result.frames.length, 2);
  const light = 0.82 / Math.hypot(0.45, 0.82, 0.35);
  const expectedPalette = result.palette.map(color => color.map(channel =>
    Math.round(Math.min(255, Math.round(channel * 255) * (light * 0.6 + 0.42)))));
  let checkedChannels = 0, maxError = 0;
  for (const frame of result.frames) {
    assert.equal(frame.pixels.length, plan.size ** 2 * 4);
    for (const [index, { x, z, rgba }] of frame.pixels.entries()) {
      assert.equal(x, Math.floor(index / 4) % plan.size);
      assert.equal(z, Math.floor(index / (4 * plan.size)));
      assert.equal(rgba.length, 4);
      const expected = [...expectedPalette[(x + z) % expectedPalette.length], 255];
      for (let channel = 0; channel < 4; channel++) {
        const error = Math.abs(rgba[channel] - expected[channel]);
        maxError = Math.max(error, maxError);
        assert(error <= plan.channelTolerance,
          `Terrain pixel (${x},${z}) channel ${channel}: expected ${expected[channel]}, got ${rgba[channel]}`);
        checkedChannels++;
      }
    }
  }
  return { passed: true, checkedChannels, maxError, scope: 'Planar terrain palette, texture upload, both triangles and time invariance; not whole-scene correctness' };
}
