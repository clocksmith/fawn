import { readFileSync, readdirSync } from 'node:fs';

// Logical ownership and Linux observations stay separate: destroying a WebGPU
// buffer is not evidence that a driver has released its submitted allocation.
export function observeMemory(phase, policy) {
  const live = new Map();
  const snapshots = [];
  const maxima = { requestedBufferBytes: 0, rssBytes: 0, arrayBuffersBytes: 0 };
  let nextId = 0;
  let overflow = false;
  let requestedBufferBytes = 0;
  const snapshot = (reason) => {
    const host = process.memoryUsage();
    const labels = {};
    for (const entry of live.values()) {
      const label = entry.label || '(unlabelled)';
      labels[label] = (labels[label] ?? 0) + entry.bytes;
    }
    const clients = new Map();
    for (const fd of readdirSync('/proc/self/fdinfo')) {
      try {
        const raw = readFileSync(`/proc/self/fdinfo/${fd}`, 'utf8');
        const id = raw.match(/^drm-client-id:\s*(.+)$/m)?.[1];
        if (id) clients.set(id, raw);
      } catch (error) {
        if (error.code !== 'ENOENT') throw error;
      }
    }
    maxima.requestedBufferBytes = Math.max(maxima.requestedBufferBytes, requestedBufferBytes);
    maxima.rssBytes = Math.max(maxima.rssBytes, host.rss);
    maxima.arrayBuffersBytes = Math.max(maxima.arrayBuffersBytes, host.arrayBuffers);
    if (snapshots.length < policy.maximumSamples) {
      snapshots.push({ elapsedMs: performance.now(), phase: phase(), reason, host,
        requestedBufferBytes, labels, drmClients: [...clients.values()] });
    } else overflow = true;
  };
  const attach = (device) => {
    const createBuffer = device.createBuffer.bind(device);
    device.createBuffer = (descriptor) => {
      const buffer = createBuffer(descriptor);
      const id = ++nextId;
      live.set(id, { device, bytes: buffer.size, label: descriptor.label ?? '' });
      requestedBufferBytes += buffer.size;
      maxima.requestedBufferBytes = Math.max(maxima.requestedBufferBytes, requestedBufferBytes);
      const destroy = buffer.destroy.bind(buffer);
      buffer.destroy = () => {
        const result = destroy();
        if (live.delete(id)) requestedBufferBytes -= buffer.size;
        return result;
      };
      return buffer;
    };
    const destroy = device.destroy.bind(device);
    device.destroy = () => {
      const result = destroy();
      for (const [id, entry] of live) if (entry.device === device) {
        live.delete(id);
        requestedBufferBytes -= entry.bytes;
      }
      snapshot('device-destroy');
      return result;
    };
  };
  const timer = setInterval(() => snapshot('interval'), policy.sampleIntervalMs);
  timer.unref();
  return { attach, snapshot, close() { clearInterval(timer); },
    result() { return { snapshots, maxima, overflow }; } };
}
