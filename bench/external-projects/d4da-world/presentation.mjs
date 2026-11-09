// Compositor consistency for one held application frame. This is not a scene oracle.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { decodePng } from '../../../browser/workspace/test/support/gpu-presentation-oracle.js';

// Evaluated in the page. Hold only the application's next tick; unrelated rAFs
// keep running so Chromium can composite and Playwright can take its screenshot.
export function installWorldPresentationCapture() {
  if (typeof doeGraphics.frameCallback !== 'function') throw new Error('No observed application frame callback');
  const applicationFrame = doeGraphics.frameCallback;
  const raf = window.requestAnimationFrame;
  const acquire = GPUCanvasContext.prototype.getCurrentTexture;
  let request = null, heldTick = null, frozen = false, sequence = 0;
  let texture = null, configuration = null, sourcePixels = null, referenceCanvas = null, originalCanvas = null;
  GPUCanvasContext.prototype.getCurrentTexture = function (...args) {
    const result = acquire.apply(this, args);
    if (this.canvas === window.document.querySelector('#field')) {
      texture = result;
      configuration = this.getConfiguration();
    }
    return result;
  };
  async function readFrame(gridSize, frameSequence) {
    if (!texture || configuration?.alphaMode !== 'opaque'
        || !['rgba8unorm', 'bgra8unorm'].includes(texture.format)) {
      throw new Error('Presentation capture requires a newly acquired opaque 8-bit canvas texture');
    }
    const canvas = document.querySelector('#field');
    const rect = canvas.getBoundingClientRect();
    if (canvas.width !== rect.width || canvas.height !== rect.height || devicePixelRatio !== 1) {
      throw new Error('Presentation capture requires unscaled canvas pixels and DPR 1');
    }
    const device = configuration.device;
    const { width, height, format } = texture;
    const runtime = window.SimulatteWorldRuntime;
    const camera = doeGraphics.sceneRenderer.camera;
    const application = { routeId: runtime.getRouteId(), state: structuredClone(runtime.diagnostics.state),
      ambientCamera: structuredClone(runtime.diagnostics.ambientCamera),
      camera: { projectionMatrix: Array.from(camera.projectionMatrix.elements),
        matrixWorld: Array.from(camera.matrixWorld.elements) } };
    const bytesPerRow = Math.ceil(width * 4 / 256) * 256;
    const buffer = device.createBuffer({ size: bytesPerRow * height,
      usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    device.pushErrorScope('validation');
    let scopeOpen = true;
    try {
      // Encode before yielding: a later task may acquire a different canvas texture.
      const encoder = device.createCommandEncoder();
      encoder.copyTextureToBuffer({ texture }, { buffer, bytesPerRow }, { width, height });
      device.queue.submit([encoder.finish()]);
      await device.queue.onSubmittedWorkDone();
      const error = await device.popErrorScope();
      scopeOpen = false;
      if (error) throw new Error(error.message);
      await buffer.mapAsync(GPUMapMode.READ);
      const data = new Uint8Array(buffer.getMappedRange());
      sourcePixels = new Uint8ClampedArray(width * height * 4);
      for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
        const source = y * bytesPerRow + x * 4, target = (y * width + x) * 4;
        sourcePixels.set(data.subarray(source, source + 4), target);
        if (format === 'bgra8unorm') [sourcePixels[target], sourcePixels[target + 2]] = [sourcePixels[target + 2], sourcePixels[target]];
      }
      const pixels = [];
      for (let row = 0; row < gridSize; row++) for (let column = 0; column < gridSize; column++) {
        const x = Math.floor((column + 0.5) * width / gridSize);
        const y = Math.floor((row + 0.5) * height / gridSize);
        const rgba = [...data.slice(y * bytesPerRow + x * 4, y * bytesPerRow + x * 4 + 4)];
        if (format === 'bgra8unorm') [rgba[0], rgba[2]] = [rgba[2], rgba[0]];
        pixels.push({ x, y, rgba, visible: true });
      }
      return { sequence: frameSequence, queueCompleted: true, width, height, format, pixels, application,
        submissions: doeGraphics.submissions, canvasAcquires: doeGraphics.canvasAcquires };
    } finally {
      if (scopeOpen) await device.popErrorScope();
      if (buffer.mapState === 'mapped') buffer.unmap();
      buffer.destroy();
    }
  }
  window.requestAnimationFrame = callback => raf(timestamp => {
    if (callback !== applicationFrame) return callback(timestamp);
    if (frozen) { heldTick = callback; return; }
    texture = null;
    callback(timestamp);
    sequence++;
    if (request) {
      frozen = true;
      const pending = request;
      request = null;
      readFrame(pending.gridSize, sequence).then(pending.resolve, pending.reject);
    }
  });
  window.doePresentation = {
    hold(gridSize) {
      if (!Number.isSafeInteger(gridSize) || gridSize < 2 || gridSize > 64) throw new Error('Invalid sample grid');
      if (request || frozen) throw new Error('A presentation capture is already active');
      return new Promise((resolve, reject) => { request = { resolve, reject, gridSize }; });
    },
    showReference() {
      if (!frozen || !sourcePixels || referenceCanvas) throw new Error('A held texture is required for the reference composite');
      originalCanvas = document.querySelector('#field');
      referenceCanvas = originalCanvas.cloneNode(false);
      referenceCanvas.getContext('2d').putImageData(new ImageData(sourcePixels, originalCanvas.width, originalCanvas.height), 0, 0);
      originalCanvas.replaceWith(referenceCanvas);
      return referenceCanvas.toDataURL('image/png').split(',')[1];
    },
    restoreReference() {
      if (referenceCanvas) referenceCanvas.replaceWith(originalCanvas);
      referenceCanvas = originalCanvas = null;
    },
    resume() {
      this.restoreReference();
      sourcePixels = null;
      request?.reject(new Error('Presentation capture cancelled'));
      request = null;
      frozen = false;
      if (heldTick) { const callback = heldTick; heldTick = null; window.requestAnimationFrame(callback); }
    },
  };
}

export function validateWorldPresentation(png, frame, after, tolerance, referencePng = null) {
  assert.equal(frame.queueCompleted, true, 'Missing GPU queue completion');
  assert(Number.isSafeInteger(frame.sequence) && frame.sequence > 0, 'Missing held frame identity');
  assert.equal(after.submissions, frame.submissions, 'GPU submission changed during capture');
  assert.equal(after.canvasAcquires, frame.canvasAcquires, 'Canvas changed during capture');
  assert(Number.isFinite(tolerance) && tolerance >= 0 && tolerance <= 2, 'Invalid pixel tolerance');
  const image = decodePng(png);
  assert.equal(image.width, frame.width);
  assert.equal(image.height, frame.height);
  const reference = referencePng ? decodePng(referencePng) : null;
  if (reference) {
    assert.equal(reference.width, image.width);
    assert.equal(reference.height, image.height);
  }
  const visible = frame.pixels.filter(pixel => pixel.visible);
  assert(visible.length > 0, 'No visible canvas samples');
  assert(new Set(visible.map(pixel => pixel.rgba.slice(0, 3).join(','))).size > 1,
    'Uniform canvas does not establish visible terrain');
  let maxError = 0;
  for (const { x, y, rgba } of visible) {
    assert(Number.isSafeInteger(x) && x >= 0 && x < image.width);
    assert(Number.isSafeInteger(y) && y >= 0 && y < image.height);
    assert.equal(rgba.length, 4);
    assert.equal(rgba[3], 255, 'Only opaque canvas samples are supported');
    for (let channel = 0; channel < 4; channel++) {
      assert(Number.isInteger(rgba[channel]) && rgba[channel] >= 0 && rgba[channel] <= 255);
      const actual = image.pixels[(y * image.width + x) * 4 + channel];
      const expected = reference ? reference.pixels[(y * image.width + x) * 4 + channel] : rgba[channel];
      const error = Math.abs(actual - expected);
      maxError = Math.max(maxError, error);
      assert(error <= tolerance, `Compositor pixel (${x},${y}) channel ${channel}: expected ${expected}, got ${actual}`);
    }
  }
  return { passed: true, sequence: frame.sequence, queueCompleted: true,
    pngSha256: createHash('sha256').update(png).digest('hex'), maxError,
    referencePngSha256: referencePng ? createHash('sha256').update(referencePng).digest('hex') : null,
    sampleCount: frame.pixels.length, visibleSampleCount: visible.length,
    scope: 'Held-frame sampled compositor consistency against texture readback composed through a 2D canvas with the same DOM/CSS; not whole-scene correctness or physical scanout' };
}
