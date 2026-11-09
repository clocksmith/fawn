import assert from 'node:assert/strict';
import vm from 'node:vm';
import { validateTerrainOracle } from './oracle.mjs';
import { observeWorld } from './observe.mjs';
import { summarizeFrames } from './process-metrics.mjs';
import { installWorldPresentationCapture, validateWorldPresentation } from './presentation.mjs';

const plan = { size: 1, channelTolerance: 2 };
const fixture = { capability: 'webgpu', palette: Array.from({ length: 8 }, () => [1, 0, 0]),
  frames: [0, 11].map(timeSec => ({ timeSec, pixels: Array.from({ length: 4 }, () =>
    ({ x: 0, z: 0, rgba: [233, 0, 0, 255] })) })) };
assert.equal(validateTerrainOracle(fixture, plan).passed, true);
for (const corrupt of [
  value => { value.frames[0].pixels[0].rgba = [0, 0, 0, 0]; },
  value => { value.frames[1].pixels[2].rgba[0] = 100; },
  value => { value.frames[0].pixels.pop(); },
  value => { value.frames[0].pixels[0].x = 1; },
  value => { value.capability = 'webgl'; },
]) {
  const damaged = structuredClone(fixture);
  corrupt(damaged);
  assert.throws(() => validateTerrainOracle(damaged, plan));
}
assert.throws(() => summarizeFrames([]));
assert.throws(() => summarizeFrames([{ intervalMs: 1, cpuWorkMs: NaN, completedMs: 2 }]));

// Independent Python/zlib encoding: opaque red and green compositor pixels.
const png = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAADklEQVR4nGP4z8DwHwQBEPgD/U6VwW8AAAAASUVORK5CYII=', 'base64');
const held = { sequence: 3, queueCompleted: true, width: 2, height: 1,
  submissions: 4, canvasAcquires: 3, pixels: [
    { x: 0, y: 0, rgba: [255, 0, 0, 255], visible: true },
    { x: 1, y: 0, rgba: [0, 255, 0, 255], visible: true },
  ] };
const after = { submissions: 4, canvasAcquires: 3 };
assert.equal(validateWorldPresentation(png, held, after, 2).visibleSampleCount, 2);
for (const corrupt of [
  frame => { frame.queueCompleted = false; },
  frame => { frame.sequence = 0; },
  frame => { frame.width = 3; },
  frame => { frame.pixels[0].rgba = [0, 0, 255, 255]; }, // previous blue frame, despite queue completion
  frame => { frame.pixels.forEach(pixel => { pixel.rgba = [0, 0, 0, 255]; }); },
  frame => { frame.pixels.forEach(pixel => { pixel.visible = false; }); },
  frame => { frame.pixels[0].rgba[3] = 0; },
  frame => { frame.pixels[0].rgba[0] = NaN; },
]) {
  const damaged = structuredClone(held);
  corrupt(damaged);
  assert.throws(() => validateWorldPresentation(png, damaged, after, 2));
}
assert.throws(() => validateWorldPresentation(png, held, { ...after, submissions: 5 }, 2), /changed during capture/);
assert.throws(() => validateWorldPresentation(png, held, { ...after, canvasAcquires: 4 }, 2), /changed during capture/);
assert.throws(() => validateWorldPresentation(png, held, after, 255), /tolerance/);

const context = vm.createContext({ performance, console });
vm.runInContext(`
  var window = globalThis;
  var localStorage = { setItem() {} };
  var document = { querySelector() { return null; } };
  var scheduled = [];
  var requestAnimationFrame = callback => scheduled.push(callback);
  class GPUQueue { submit() {} onSubmittedWorkDone() { return Promise.resolve(); } }
  class GPUDevice { destroy() {} createShaderModule(value) { return value; } }
  class GPUAdapter { requestDevice() { return Promise.resolve(new GPUDevice()); } }
  class GPUCanvasContext { getCurrentTexture() { return {}; } }
  Object.assign(globalThis, { GPUQueue, GPUDevice, GPUAdapter, GPUCanvasContext });
  var queue = new GPUQueue();
  var canvasContext = new GPUCanvasContext();
  var sceneRenderer = { renderer: { backend: { device: { queue } } }, draw() { queue.submit([]); canvasContext.getCurrentTexture(); } };
  var tick = function tick() { sceneRenderer.draw({ mesh: { tileAlpha: 0.7 } }); };
  var unrelated = function tick() {};
`, context);
vm.runInContext(`(${observeWorld.toString()})();`, context);
await vm.runInContext(`
  SimulatteWorldRendererFactory = Object.freeze({ async createSceneRenderer() { return sceneRenderer; } });
  SimulatteWorldRendererFactory.createSceneRenderer({ canvas: { id: 'field' } });
`, context);
vm.runInContext(`
  doeGraphics.active = true;
  requestAnimationFrame(unrelated);
  requestAnimationFrame(tick);
  scheduled.forEach(callback => callback(100));
`, context);
await vm.runInContext('Promise.all(doeGraphics.pending)', context);
const rows = vm.runInContext('doeGraphics.frames', context);
assert.equal(rows.length, 1, 'Another callback named tick must not become an application frame');
assert.equal(rows[0].submissions, 1);
assert.equal(rows[0].canvasAcquires, 1);
assert.equal(vm.runInContext('doeGraphics.actualTileAlpha', context), 0.7);
assert.equal(vm.runInContext('doeGraphics.frameCallback === tick', context), true);
assert(rows[0].completedMs >= rows[0].cpuWorkMs);
vm.runInContext(`scheduled = []; (${installWorldPresentationCapture.toString()})();`, context);
// The observer sees the capture wrapper after installation. Capturing again
// must still recognize the original application callback, not the wrapper.
for (let capture = 0; capture < 2; capture++) {
  const pending = vm.runInContext('doePresentation.hold(2)', context);
  const rejection = assert.rejects(pending, /newly acquired opaque/);
  vm.runInContext('requestAnimationFrame(tick); scheduled.splice(0).forEach(callback => callback(200));', context);
  await rejection; // reaches the readback boundary; this mock deliberately has no real texture
  const count = vm.runInContext('doeGraphics.submissions', context);
  vm.runInContext('requestAnimationFrame(tick); scheduled.splice(0).forEach(callback => callback(201));', context);
  assert.equal(vm.runInContext('doeGraphics.submissions', context), count, 'Held frame must not submit new draws');
  vm.runInContext('doePresentation.resume(); scheduled.splice(0).forEach(callback => callback(202));', context);
  assert.equal(vm.runInContext('doeGraphics.submissions', context), count + 1, 'Resume restores the held callback');
}
vm.runInContext('var device = new GPUDevice(); device.destroy(); device.destroy();', context);
assert.equal(vm.runInContext('doeGraphics.destroyedDevices', context), 1);
console.log('PASS: independent terrain and compositor pixel rejection, stale-frame controls, timing validation, frame observer and destruction accounting');
