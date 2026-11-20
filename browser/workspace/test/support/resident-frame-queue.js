// Runs in an isolated test page with a real WebGPU device and VideoFrames.
export async function sampleFrameQueue(createReader) {
  const reader = createReader('fixture');
  const canvas = new OffscreenCanvas(128, 64);
  const ctx = canvas.getContext('2d', { alpha: false });
  ctx.fillStyle = 'red';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  const originalWait = GPUQueue.prototype.onSubmittedWorkDone;
  const originalMap = GPUBuffer.prototype.mapAsync;
  let waits = 0, delayed, enter, failMap = false, sequence = 0;
  GPUQueue.prototype.onSubmittedWorkDone = function (...args) {
    waits++;
    const completed = originalWait.apply(this, args);
    if (!delayed) return completed;
    enter();
    return Promise.all([completed, delayed]).then(() => {});
  };
  GPUBuffer.prototype.mapAsync = function (...args) {
    if (failMap) {
      failMap = false;
      return Promise.reject(Error('fixture mask map failed'));
    }
    return originalMap.apply(this, args);
  };
  async function sample() {
    const before = waits;
    const frame = new VideoFrame(canvas, { timestamp: ++sequence });
    try {
      const row = await reader.readFrame('same region', frame, false, 'external-texture');
      return { hashes: row.hashes, rehashed: row.rehashedTiles, maskBytes: row.gpuMaskReadbackBytes, waits: waits - before };
    } finally { frame.close(); }
  }
  let release;
  try {
    const first = await sample();
    const retained = await sample();
    ctx.fillStyle = 'lime';
    ctx.fillRect(100, 21, 1, 1);
    const changed = await sample();
    failMap = true;
    const waiting = new Promise(resolve => { enter = resolve; });
    delayed = new Promise(resolve => { release = resolve; });
    const before = waits;
    let error = null;
    const failed = sample().then(() => { throw Error('expected map to fail'); }, reason => { error = reason.message; });
    await Promise.race([waiting, failed]);
    const rejectedBeforeDrain = error !== null;
    release();
    await failed;
    return { first, retained, changed, onError: { waits: waits - before, rejectedBeforeDrain, error }, adapter: reader.adapterInfo() };
  } finally {
    release?.();
    GPUQueue.prototype.onSubmittedWorkDone = originalWait;
    GPUBuffer.prototype.mapAsync = originalMap;
    reader.dispose();
  }
}
