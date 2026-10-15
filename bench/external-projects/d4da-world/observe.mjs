// Observe the application's existing tick and WebGPU calls without rewriting its assets.
export function observeWorld() {
  localStorage.setItem('d4da-renderer-preference', 'webgpu');
  const evidence = window.doeGraphics = {
    frames: [], shaders: [], adapters: [], errors: [], createdDevices: 0, destroyedDevices: 0,
    submissions: 0, canvasAcquires: 0, active: false, pending: [],
  };
  if (!globalThis.GPUAdapter) throw new Error('WebGPU is unavailable');
  const requestDevice = GPUAdapter.prototype.requestDevice;
  GPUAdapter.prototype.requestDevice = async function (...args) {
    const device = await requestDevice.apply(this, args);
    const { vendor, architecture, device: deviceId, description } = this.info;
    evidence.adapters.push({ vendor, architecture, device: deviceId, description });
    evidence.createdDevices++;
    device.addEventListener('uncapturederror', event => evidence.errors.push(event.error.message));
    device.lost.then(info => {
      if (info.reason !== 'destroyed') evidence.errors.push(`Device lost: ${info.message}`);
    });
    return device;
  };
  const destroyed = new WeakSet();
  const destroy = GPUDevice.prototype.destroy;
  GPUDevice.prototype.destroy = function (...args) {
    const result = destroy.apply(this, args);
    if (!destroyed.has(this)) { destroyed.add(this); evidence.destroyedDevices++; }
    return result;
  };
  const createShaderModule = GPUDevice.prototype.createShaderModule;
  GPUDevice.prototype.createShaderModule = function (descriptor) {
    evidence.shaders.push({ label: descriptor.label || '', code: descriptor.code });
    return createShaderModule.call(this, descriptor);
  };
  const submit = GPUQueue.prototype.submit;
  GPUQueue.prototype.submit = function (...args) {
    evidence.submissions++;
    return submit.apply(this, args);
  };
  const acquire = GPUCanvasContext.prototype.getCurrentTexture;
  GPUCanvasContext.prototype.getCurrentTexture = function (...args) {
    evidence.canvasAcquires++;
    return acquire.apply(this, args);
  };
  const raf = window.requestAnimationFrame.bind(window);
  let previous = null;
  window.requestAnimationFrame = callback => raf(timestamp => {
    if (callback !== window.tick || !evidence.active) return callback(timestamp);
    const started = performance.now();
    const submissions = evidence.submissions, acquires = evidence.canvasAcquires;
    callback(timestamp);
    const row = { timestamp, intervalMs: previous === null ? null : timestamp - previous,
      cpuWorkMs: performance.now() - started,
      submissions: evidence.submissions - submissions,
      canvasAcquires: evidence.canvasAcquires - acquires, completedMs: null };
    previous = timestamp;
    evidence.frames.push(row);
    // Only the scene's device: capability-probe devices may already be destroyed.
    const device = sceneRenderer.renderer.backend.device;
    evidence.pending.push(device.queue.onSubmittedWorkDone().then(() => {
      row.completedMs = performance.now() - started;
    }, error => evidence.errors.push(String(error))));
  });
}
