// Fawn background cycle: Flows, Magnets, and Particles on one canvas with one visible-time clock.
// This file owns the page lifecycle; fawn-scenes.js owns everything drawn on the GPU.
import { CYCLE_POLICY, createCycleClock, sceneWeight } from './fawn-cycle-clock.js';
import { createPointer } from './fawn-input.js';
import { createMeter } from './fawn-meter.js';
import { SCENE_POLICY, createScenes } from './fawn-scenes.js';

const HOST_POLICY = Object.freeze({
  maxDpr: 2,
  mobileQuery: '(max-width: 768px), (pointer: coarse)',
  gpuRecoveries: 2,
});
const $ = (selector) => document.querySelector(selector);
const canvas = $('#view');
const controls = Object.freeze({ count: $('#count-select'), steps: $('#steps-select'),
  formation: $('#formation-select') });
const clock = createCycleClock();
const formation = FawnFormation.transition(CYCLE_POLICY.formation);
const meter = createMeter($('#frame'), $('#fps-graph'));
let device = null, context = null, format = null, scenes = null;
let running = false, paused = matchMedia('(prefers-reduced-motion: reduce)').matches;
let raf = 0, pending = false, redraw = false, configured = false;
let lastTime = 0, time = 0, generation = 0, recoveries = 0, shownEffect = null;
const pointer = createPointer(canvas, {
  magnetWeight: () => (paused ? 0 : sceneWeight('wave', clock.state())),
  onInput: () => schedule(),
});
let logoEnabled = FawnFormation.configure(controls.formation);
FawnFormation.configureWorkload(controls.count, controls.steps, 'cycle',
  matchMedia(HOST_POLICY.mobileQuery).matches);
const steps = () => Number(controls.steps.value);

function showEffect(effect) {
  if (effect === shownEffect) return;
  shownEffect = effect;
  $('#effect').textContent = CYCLE_POLICY.labels[effect];
  canvas.setAttribute('aria-label', `${CYCLE_POLICY.labels[effect]}. Drag to stir.`);
}

function describe() {
  const count = Number(controls.count.value);
  $('#status').textContent = paused ? 'Paused' : 'WebGPU active';
  $('#load').textContent = `${(Math.ceil(Math.sqrt(count)) ** 2).toLocaleString()} cells / `
    + `${count.toLocaleString()} particles · ${steps()} steps`;
}

function resize() {
  const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
  const scale = Math.min(device.limits.maxTextureDimension2D / side, devicePixelRatio, HOST_POLICY.maxDpr);
  const width = Math.max(1, Math.round(canvas.clientWidth * scale));
  const height = Math.max(1, Math.round(canvas.clientHeight * scale));
  if (configured && canvas.width === width && canvas.height === height) return;
  canvas.width = width;
  canvas.height = height;
  context.configure({ device, format, alphaMode: 'opaque' });
  configured = true;
  scenes?.resize();
}

// Restart the cycle from Flows with freshly seeded GPU state; every scene keeps its own physics.
function reset() {
  generation++;
  clock.reset();
  formation.reset(logoEnabled);
  pointer.clear();
  time = lastTime = 0;
  scenes?.destroy();
  scenes = createScenes(device, canvas, format, Number(controls.count.value));
  meter.clear();
  describe();
  showEffect(CYCLE_POLICY.effects[0]);
  schedule();
}

function schedule() {
  if (!running || document.hidden) return;
  if (pending) {
    redraw = true;
    return;
  }
  if (!raf) raf = requestAnimationFrame(frame);
}

async function frame(now) {
  raf = 0;
  if (!running || document.hidden) return;
  const frameGeneration = generation;
  pending = true;
  try {
    resize();
    // A stalled frame still advances the visible clock so blends and handoffs are never skipped;
    // only the simulation step is capped.
    const elapsed = paused || !lastTime ? 0
      : Math.min(CYCLE_POLICY.maxFrameS, Math.max(0, (now - lastTime) / 1000));
    const dt = Math.min(CYCLE_POLICY.maxDtS, elapsed);
    lastTime = now;
    const cycle = clock.advance(elapsed);
    pointer.advance(elapsed, sceneWeight('wave', cycle));
    if (cycle.completed) formation.retarget();
    const interacting = pointer.interacting;
    const shape = formation.advance(elapsed, logoEnabled, interacting || cycle.transitioning, interacting,
      cycle.blend);
    time += dt;
    const encoder = device.createCommandEncoder({ label: 'Fawn frame' });
    scenes.draw(encoder, context.getCurrentTexture().createView(), {
      cycle, dt, time, steps: steps(), shape, pointer: pointer.state, burst: pointer.burst,
    });
    device.queue.submit([encoder.finish()]);
    pointer.endFrame();
    showEffect(cycle.effect);
    await device.queue.onSubmittedWorkDone();
    if (running && !paused && frameGeneration === generation) meter.complete(performance.now());
  } catch (error) {
    fail(error);
  } finally {
    pending = false;
    if (!paused || redraw) {
      redraw = false;
      schedule();
    }
  }
}

function stop() {
  running = false;
  generation++;
  cancelAnimationFrame(raf);
  raf = 0;
  pointer.clear();
  scenes?.destroy();
  scenes = null;
}

function fail(error) {
  stop();
  $('#status').textContent = 'Unavailable';
  const failure = $('#failure');
  failure.hidden = false;
  failure.textContent = error.message || String(error);
  Object.values(controls).forEach((control) => { control.disabled = true; });
  context?.unconfigure();
  device?.destroy();
}

function toggleMotion() {
  paused = !paused;
  generation++;
  lastTime = 0;
  meter.restart(paused ? 'Paused' : '— fps');
  describe();
  if (paused) {
    pointer.clear();
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
}

// A lost device restarts the cycle on a new one; repeated losses surface the failure instead.
function recover(info) {
  if (!running || info.reason === 'destroyed') return;
  if (recoveries++ >= HOST_POLICY.gpuRecoveries) {
    fail(new Error(`GPU connection lost: ${info.message || info.reason}`));
    return;
  }
  stop();
  start();
}

async function start() {
  try {
    if (!navigator.gpu) throw new Error('WebGPU is unavailable in this browser.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter is available.');
    device = await adapter.requestDevice();
    device.lost.then(recover);
    device.addEventListener('uncapturederror', (event) => { if (running) fail(event.error); });
    format = navigator.gpu.getPreferredCanvasFormat();
    context = canvas.getContext('webgpu');
    configured = false;
    if (!context) throw new Error('WebGPU canvas is unavailable.');
    const storageLimit = Math.min(device.limits.maxStorageBufferBindingSize, device.limits.maxBufferSize);
    FawnFormation.limitCount(controls.count,
      Math.min(SCENE_POLICY.maxCount, Math.floor(storageLimit / SCENE_POLICY.particleBytes)));
    $('#adapter').textContent = [adapter.info?.vendor, adapter.info?.architecture].filter(Boolean)
      .join(' · ') || 'WebGPU connected';
    Object.values(controls).forEach((control) => { control.disabled = false; });
    resize();
    running = true;
    reset();
  } catch (error) {
    fail(error);
  }
}

controls.count.addEventListener('change', () => reset());
controls.steps.addEventListener('change', () => {
  describe();
  schedule();
});
controls.formation.addEventListener('change', () => {
  logoEnabled = controls.formation.value === 'branded';
  formation.retarget();
  schedule();
});
canvas.addEventListener('keydown', (event) => {
  if (event.code === 'Space') {
    event.preventDefault();
    toggleMotion();
  } else if (event.key.toLowerCase() === 'r') {
    event.preventDefault();
    reset();
  }
});
document.addEventListener('visibilitychange', () => {
  generation++;
  lastTime = 0;
  pointer.clear();
  meter.restart(paused ? 'Paused' : '— fps');
  if (document.hidden) {
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
});
addEventListener('blur', () => {
  pointer.clear();
  schedule();
});
addEventListener('resize', () => schedule());
new ResizeObserver(() => schedule()).observe(canvas);
addEventListener('pagehide', () => {
  stop();
  device?.destroy();
});
// A page restored from the back-forward cache has no device left to draw with.
addEventListener('pageshow', (event) => { if (event.persisted) location.reload(); });
start();
