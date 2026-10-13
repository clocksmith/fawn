// Fullscreen cycle: one device, one canvas, one visible-time clock, resident GPU handoffs.
import { CYCLE_POLICY, createCycleClock } from './fawn-cycle-clock.js';
import { GPU_POLICY, createCycleGpu } from './fawn-cycle-gpu.js';
const HOST_POLICY = Object.freeze({ version: 1, maxDpr: 2, metricS: 0.5, graphSamples: 60 });
const canvas = document.querySelector('#view');
const status = document.querySelector('#status');
const pause = document.querySelector('#pause');
const failure = document.querySelector('#failure');
const countSelect = document.querySelector('#count-select');
const stepsSelect = document.querySelector('#steps-select');
const formationSelect = document.querySelector('#formation-select');
const graph = document.querySelector('#fps-graph'),
  graphContext = graph.getContext('2d');
const clock = createCycleClock(),
  formation = FawnFormation.transition();
let logoEnabled = FawnFormation.configure(formationSelect);
FawnFormation.configureWorkload(countSelect, stepsSelect, 'flow');
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
const pointer = {
  x: 0.5,
  y: 0.5,
  dx: 0,
  dy: 0,
  hover: false,
  down: false,
  press: 0,
  moved: false,
  id: null,
};
const fps = [];
function updateControls() {
  pause.textContent = paused ? 'Resume' : 'Pause';
  pause.setAttribute('aria-pressed', String(paused));
  status.textContent = paused ? 'Paused' : 'WebGPU active';
  document.querySelector('#load').textContent =
    `${Number(countSelect.value).toLocaleString()} cells / particles · ${stepsSelect.value} steps`;
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
  clearPointer();
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
  document.querySelector('#effect').textContent = 'Flow';
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
    const dt =
      paused || !lastTime ? 0 : Math.min(CYCLE_POLICY.maxDtS, Math.max(0, (now - lastTime) / 1000));
    lastTime = now;
    const interacting = pointer.down || pointer.moved;
    const cycle = clock.advance(dt, interacting);
    shape = formation.advance(dt, logoEnabled, interacting || cycle.transitioning);
    const liveDt = dt * (1 - shape);
    time += liveDt;
    const weights = CYCLE_POLICY.effects.map(
      (effect) =>
        (effect === cycle.effect ? 1 - cycle.blend : 0) +
        (cycle.transitioning && effect === cycle.next ? cycle.blend : 0),
    );
    gpu.uniforms(dt, time, shape, pointer, Number(stepsSelect.value), weights);
    const encoder = device.createCommandEncoder({ label: 'Fawn cycle frame' });
    if (cycle.started) gpu.handoff(encoder, cycle.effect);
    if (liveDt > 0) {
      gpu.simulate(encoder, cycle.effect, Number(stepsSelect.value));
      if (cycle.transitioning) gpu.simulate(encoder, cycle.next, Number(stepsSelect.value));
    }
    if (dt > 0) gpu.advanceParcels(encoder);
    gpu.draw(encoder, context.getCurrentTexture().createView(), weights, time);
    device.queue.submit([encoder.finish()]);
    pointer.dx = 0;
    pointer.dy = 0;
    pointer.press = 0;
    pointer.moved = false;
    if (cycle.completed) formation.retarget();
    document.querySelector('#effect').textContent =
      cycle.effect[0].toUpperCase() + cycle.effect.slice(1);
    canvas.setAttribute('aria-label', `${cycle.effect}. Drag to stir.`);
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
    pause,
    countSelect,
    stepsSelect,
    formationSelect,
    document.querySelector('#restart'),
  ])
    control.disabled = true;
  gpu?.destroy();
  gpu = null;
  context?.unconfigure();
  device?.destroy();
}
function clearPointer() {
  if (pointer.id !== null && canvas.hasPointerCapture(pointer.id))
    canvas.releasePointerCapture(pointer.id);
  Object.assign(pointer, {
    id: null,
    down: false,
    hover: false,
    dx: 0,
    dy: 0,
    press: 0,
    moved: false,
  });
}
function point(event) {
  const [x, y] = FawnFormation.screenPoint(event, canvas);
  if (pointer.hover || pointer.down) {
    pointer.dx += x - pointer.x;
    pointer.dy += y - pointer.y;
  }
  pointer.x = x;
  pointer.y = y;
  pointer.moved = true;
}
canvas.addEventListener('pointerdown', (event) => {
  if (pointer.id !== null && pointer.id !== event.pointerId) return;
  pointer.id = event.pointerId;
  point(event);
  pointer.down = true;
  pointer.hover = true;
  pointer.press = 1;
  canvas.setPointerCapture(event.pointerId);
  canvas.focus({ preventScroll: true });
  schedule();
});
canvas.addEventListener('pointermove', (event) => {
  if (pointer.id !== null && pointer.id !== event.pointerId) return;
  point(event);
  pointer.hover = true;
  schedule();
});
function release(event) {
  if (pointer.id !== event.pointerId) return;
  pointer.down = false;
  pointer.press = 0;
  pointer.id = null;
  const rect = canvas.getBoundingClientRect();
  pointer.hover =
    event.type === 'pointerup' &&
    event.pointerType === 'mouse' &&
    event.clientX >= rect.left &&
    event.clientX <= rect.right &&
    event.clientY >= rect.top &&
    event.clientY <= rect.bottom;
  schedule();
}
canvas.addEventListener('pointerup', release);
canvas.addEventListener('pointercancel', release);
canvas.addEventListener('lostpointercapture', release);
canvas.addEventListener('pointerleave', () => {
  pointer.hover = false;
  if (!pointer.down) pointer.moved = false;
});
pause.addEventListener('click', () => {
  paused = !paused;
  generation++;
  lastTime = 0;
  lastCompletion = 0;
  sampleTime = 0;
  samples = 0;
  document.querySelector('#frame').textContent = paused ? 'Paused' : '— fps';
  updateControls();
  if (paused) {
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
});
document.querySelector('#restart').addEventListener('click', () => {
  reset();
});
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
  clearPointer();
  if (document.hidden) {
    cancelAnimationFrame(raf);
    raf = 0;
  } else schedule();
});
canvas.addEventListener('keydown', (event) => {
  if (event.code === 'Space') {
    event.preventDefault();
    pause.click();
  } else if (event.key.toLowerCase() === 'r') {
    event.preventDefault();
    reset();
  }
});
addEventListener('blur', () => {
  clearPointer();
  schedule();
});
addEventListener('resize', () => schedule());
new ResizeObserver(() => schedule()).observe(canvas);
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
    for (const control of [
      pause,
      countSelect,
      stepsSelect,
      formationSelect,
      document.querySelector('#restart'),
    ])
      control.disabled = false;
    resize();
    running = true;
    reset();
  } catch (error) {
    fail(error);
  }
})();
