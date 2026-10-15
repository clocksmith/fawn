import assert from 'node:assert/strict';
import vm from 'node:vm';
import { validateTerrainOracle } from './oracle.mjs';
import { observeWorld } from './observe.mjs';
import { summarizeFrames } from './process-metrics.mjs';

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

const context = vm.createContext({ performance, console });
vm.runInContext(`
  var window = globalThis;
  var localStorage = { setItem() {} };
  var scheduled = [];
  var requestAnimationFrame = callback => scheduled.push(callback);
  class GPUQueue { submit() {} onSubmittedWorkDone() { return Promise.resolve(); } }
  class GPUDevice { destroy() {} createShaderModule(value) { return value; } }
  class GPUAdapter { requestDevice() { return Promise.resolve(new GPUDevice()); } }
  class GPUCanvasContext { getCurrentTexture() { return {}; } }
  Object.assign(globalThis, { GPUQueue, GPUDevice, GPUAdapter, GPUCanvasContext });
  var queue = new GPUQueue();
  var canvasContext = new GPUCanvasContext();
  var sceneRenderer = { renderer: { backend: { device: { queue } } } };
  var tick = function tick() { queue.submit([]); canvasContext.getCurrentTexture(); };
  var unrelated = function tick() {};
`, context);
vm.runInContext(`(${observeWorld.toString()})();
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
assert(rows[0].completedMs >= rows[0].cpuWorkMs);
vm.runInContext('var device = new GPUDevice(); device.destroy(); device.destroy();', context);
assert.equal(vm.runInContext('doeGraphics.destroyedDevices', context), 1);
console.log('PASS: independent pixel rejection, timing validation, exact frame observer and destruction accounting');
