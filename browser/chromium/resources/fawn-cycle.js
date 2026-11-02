// Fullscreen cycle: one device, one canvas, one visible-time clock, resident GPU handoffs.
import { CYCLE_POLICY, createCycleClock } from './fawn-cycle-clock.js';
import { GPU_POLICY, createCycleGpu } from './fawn-cycle-gpu.js';
import { SCENES } from './fawn-scenes.js';
import { createCycleInteraction } from './fawn-cycle-interaction.js';
const HOST_POLICY = Object.freeze({ version: 2, maxDpr: 2, metricS: 0.5, graphSamples: 60 });
const canvas = document.querySelector('#view');
const status = document.querySelector('#status');
const failure = document.querySelector('#failure');
const countSelect = document.querySelector('#count-select');
const stepsSelect = document.querySelector('#steps-select');
const formationSelect = document.querySelector('#formation-select');
const graph = document.querySelector('#fps-graph'),
  graphContext = graph.getContext('2d');
const clock = createCycleClock(),
  formation = FawnFormation.transition(CYCLE_POLICY.formation);
let logoEnabled = FawnFormation.configure(formationSelect);
FawnFormation.configureWorkload(
  countSelect,
  stepsSelect,
  'cycle',
  matchMedia('(max-width: 768px), (pointer: coarse)').matches,
);
let device, context, gpu, format;
let configured = false,
  refreshPending = false;
let running = false,
  paused = matchMedia('(prefers-reduced-motion: reduce)').matches;
let raf = 0,
  lastTime = 0,
  time = 0,
  pending = false,
  shape = 0;
let sampleTime = 0,
  samples = 0,
  lastCompletion = 0,
  generation = 0;
const interaction = createCycleInteraction(canvas, {
  schedule, isPaused: () => paused, weights: () => clock.state().weights,
});
const { pointer, burst } = interaction;
const fps = [];
function updateControls() {
  status.textContent = paused ? 'Paused' : 'WebGPU active';
  document.querySelector('#load').textContent =
    `${(Math.ceil(Math.sqrt(Number(countSelect.value))) ** 2).toLocaleString()} cells / ${Number(countSelect.value).toLocaleString()} particles · ${stepsSelect.value} steps`;
}
function resize() {
  const side = Math.max(canvas.clientWidth, canvas.clientHeight, 1);
  const scale = Math.min(
    device.limits.maxTextureDimension2D / side,
    devicePixelRatio,
    HOST_POLICY.maxDpr,
  );
  const width = Math.max(1, Math.round(canvas.clientWidth * scale));
  const height = Math.max(1, Math.round(canvas.clientHeight * scale));
  if (configured && canvas.width === width && canvas.height === height) return;
  canvas.width = width;
  canvas.height = height;
  context.configure({ device, format, alphaMode: 'opaque' });
  configured = true;
  if (gpu) {
    gpu.resize();
  }
}
function reset() {
  generation++;
  clock.reset();
  formation.reset(logoEnabled);
  shape = 0;
  time = 0;
  interaction.clear();
  if (gpu) {
    gpu.destroy();
    gpu = null;
  }
  gpu = createCycleGpu(device, canvas, format, Number(countSelect.value));
  updateControls();
  lastTime = 0;
  lastCompletion = 0;
  samples = 0;
  sampleTime = 0;
  fps.length = 0;
  document.querySelector('#effect').textContent = SCENES.flow.label;
  schedule();
}
function meter(now) {
  if (lastCompletion) {
    sampleTime += (now - lastCompletion) / 1000;
    samples++;
  }
  lastCompletion = now;
  if (sampleTime < HOST_POLICY.metricS) return;
  const value = samples / sampleTime;
  document.querySelector('#frame').textContent = `${Math.round(value)} fps`;
  fps.push(value);
  if (fps.length > HOST_POLICY.graphSamples) fps.shift();
  samples = 0;
  sampleTime = 0;
  const dpr = Math.min(devicePixelRatio, HOST_POLICY.maxDpr);
  graph.width = Math.round(graph.clientWidth * dpr);
  graph.height = Math.round(graph.clientHeight * dpr);
  graphContext.clearRect(0, 0, graph.width, graph.height);
  graphContext.strokeStyle = '#ffffff80';
  graphContext.lineWidth = dpr;
  graphContext.beginPath();
  const ceiling = Math.max(60, ...fps);
  fps.forEach((v, i) => {
    const x = (i / (HOST_POLICY.graphSamples - 1)) * graph.width,
      y = graph.height * (1 - v / ceiling);
    if (i) graphContext.lineTo(x, y);
    else graphContext.moveTo(x, y);
  });
  graphContext.stroke();
}
function schedule() {
  if (!running || document.hidden) return;
  if (pending) {
    refreshPending = true;
    return;
  }
  if (!raf) raf = requestAnimationFrame(frame);
}
async function frame(now) {
  raf = 0;
  if (!running || document.hidden) return;
  const currentGeneration = generation;
  pending = true;
  try {
    resize();
    // A stalled frame must not skip the visible blend or its GPU handoff.
    const elapsedDt = paused || !lastTime ? 0 :
      Math.min(CYCLE_POLICY.maxFrameS, Math.max(0, (now - lastTime) / 1000));
    const dt = Math.min(CYCLE_POLICY.maxDtS, elapsedDt);
    lastTime = now;
    const cycle = clock.advance(elapsedDt);
    const { weights } = cycle;
    const interacting = interaction.advance(elapsedDt, weights);
    if (cycle.completed) formation.retarget();
    shape = formation.advance(
      elapsedDt, logoEnabled, cycle.transitioning, interacting, cycle.blend,
    );
    time += dt;
    gpu.uniforms(dt, time, shape, pointer, Number(stepsSelect.value), weights, burst);
    const encoder = device.createCommandEncoder({ label: 'Fawn cycle frame' });
    if (cycle.started) gpu.prepareScene(encoder, cycle.next);
    if (dt > 0) {
      gpu.simulate(encoder, cycle.effect, Number(stepsSelect.value));
      if (cycle.transitioning) gpu.simulate(encoder, cycle.next, Number(stepsSelect.value));
    }
    if (dt > 0) gpu.advanceParcels(encoder);
    gpu.draw(encoder, context.getCurrentTexture().createView(), weights, time, cycle.elapsedS, shape);
    device.queue.submit([encoder.finish()]);
    interaction.consume();
    document.querySelector('#effect').textContent = SCENES[cycle.effect].label;
    canvas.setAttribute('aria-label', `${SCENES[cycle.effect].label}. Drag to stir.`);
    await device.queue.onSubmittedWorkDone();
    if (!running || currentGeneration !== generation) return;
    if (!paused) meter(performance.now());
  } catch (error) {
    fail(error);
  } finally {
    pending = false;
    if (!paused || refreshPending) {
      refreshPending = false;
      schedule();
    }
  }
}
function fail(error) {
  running = false;
  generation++;
  cancelAnimationFrame(raf);
  raf = 0;
  status.textContent = 'Unavailable';
  failure.hidden = false;
  failure.textContent = error.message || String(error);
  for (const control of [
    countSelect,
    stepsSelect,
    formationSelect,
  ])
    control.disabled = true;
  gpu?.destroy();
  gpu = null;
  context?.unconfigure();
  device?.destroy();
}
function toggleMotion() {
  paused = !paused;
  generation++;
  lastTime = 0;
  lastCompletion = 0;
  sampleTime = 0;
  samples = 0;
  document.querySelector('#frame').textContent = paused ? 'Paused' : '— fps';
  updateControls();
  if (paused) {
    interaction.clear();
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
}
countSelect.addEventListener('change', () => reset());
stepsSelect.addEventListener('change', () => {
  updateControls();
  schedule();
});
formationSelect.addEventListener('change', () => {
  logoEnabled = formationSelect.value === 'branded';
  formation.retarget();
  schedule();
});
document.addEventListener('visibilitychange', () => {
  generation++;
  lastTime = 0;
  lastCompletion = 0;
  interaction.clear();
  if (document.hidden) {
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
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
addEventListener('blur', () => {
  interaction.clear();
  schedule();
});
addEventListener('resize', () => schedule());
new ResizeObserver(() => schedule()).observe(canvas);
// A page restored from the back-forward cache has no device left to draw with.
addEventListener('pageshow', (event) => { if (event.persisted) location.reload(); });
addEventListener('pagehide', () => {
  running = false;
  generation++;
  cancelAnimationFrame(raf);
  gpu?.destroy();
  device?.destroy();
});
(async () => {
  try {
    if (!navigator.gpu) throw new Error('WebGPU is unavailable in this browser.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter is available.');
    device = await adapter.requestDevice();
    device.lost.then((info) => {
      if (running) fail(new Error(`GPU connection lost: ${info.message || info.reason}`));
    });
    device.addEventListener('uncapturederror', (event) => {
      if (running) fail(event.error);
    });
    format = navigator.gpu.getPreferredCanvasFormat();
    context = canvas.getContext('webgpu');
    if (!context) throw new Error('WebGPU canvas is unavailable.');
    FawnFormation.limitCount(
      countSelect,
      Math.min(
        GPU_POLICY.maxCount,
        Math.floor(
          Math.min(device.limits.maxStorageBufferBindingSize, device.limits.maxBufferSize) /
            GPU_POLICY.parcelBytes,
        ),
      ),
    );
    document.querySelector('#adapter').textContent =
      [adapter.info?.vendor, adapter.info?.architecture].filter(Boolean).join(' · ') ||
      'WebGPU connected';
    for (const control of [countSelect, stepsSelect, formationSelect])
      control.disabled = false;
    resize();
    running = true;
    reset();
  } catch (error) {
    fail(error);
  }
})();
