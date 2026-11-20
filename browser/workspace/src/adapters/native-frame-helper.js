// The helper runs outside Chromium and receives only the already-restricted
// frame. Buffer.equals uses native byte comparison; SHA-256 runs in OpenSSL.
import { createHash } from 'node:crypto';
import { Worker, parentPort, workerData } from 'node:worker_threads';

const maxPixels = 4 * 1024 * 1024;

// Retain one image per live grant; the worker exits when that grant ends.
function compareTiles(current, prior, width, height, verify) {
  let tileSize = 64;
  while (Math.ceil(width / tileSize) * Math.ceil(height / tileSize) > 256) tileSize *= 2;
  let hashedBytes = 0, rehashedTiles = 0;
  const hashes = [], stride = width * 4;
  const sameImage = !verify && prior && current.equals(prior.pixels);
  if (sameImage) return { hashes: prior.hashes, tileSize, hashedBytes, rehashedTiles };
  for (let top = 0; top < height; top += tileSize) {
    const bottom = Math.min(height, top + tileSize);
    for (let x = 0; x < width; x += tileSize) {
      const bytes = Math.min(tileSize, width - x) * 4;
      let unchanged = Boolean(prior) && !verify;
      for (let y = top; unchanged && y < bottom; y++) {
        const offset = y * stride + x * 4;
        unchanged = current.subarray(offset, offset + bytes).equals(prior.pixels.subarray(offset, offset + bytes));
      }
      if (unchanged) { hashes.push(prior.hashes[hashes.length]); continue; }
      rehashedTiles++;
      const hash = createHash('sha256');
      for (let y = top; y < bottom; y++) {
        const offset = y * stride + x * 4;
        hash.update(current.subarray(offset, offset + bytes));
      }
      hashedBytes += (bottom - top) * bytes;
      hashes.push(hash.digest('hex'));
    }
  }
  return { hashes, tileSize, hashedBytes, rehashedTiles };
}

if (workerData?.blitzenNativeFrameHelper === true) {
  let previous;
  parentPort.on('message', ({ buffer, width, height, verify }) => {
    const pixels = Buffer.from(buffer);
    const prior = previous?.width === width && previous?.height === height ? previous : null;
    const result = compareTiles(pixels, prior, width, height, verify);
    result.peakRgbaBytes = pixels.byteLength + (previous?.pixels.byteLength ?? 0);
    previous?.pixels.fill(0);
    previous = { pixels, width, height, hashes: result.hashes };
    parentPort.postMessage(result);
  });
}

export class NativeFrameHelper {
  worker = new Worker(new URL('./native-frame-helper.js', import.meta.url), {
    workerData: { blitzenNativeFrameHelper: true }, execArgv: [], env: {},
  });
  pending = null;
  closed = false;

  constructor() {
    this.worker.on('message', result => {
      const waiter = this.pending;
      this.pending = null;
      waiter?.resolve(result);
    });
    const fail = error => {
      this.closed = true;
      this.pending?.reject(error);
      this.pending = null;
    };
    this.worker.on('error', fail);
    this.worker.on('exit', () => fail(Error('Native frame helper ended')));
  }

  async sample(width, height, encoded, verify) {
    const size = width * height * 4;
    if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width < 1 || height < 1
      || size > 4 * maxPixels || typeof encoded !== 'string' || encoded.length !== Math.ceil(size / 3) * 4) {
      throw Error('Invalid restricted frame dimensions or length');
    }
    if (this.closed || this.pending) throw Error('Native frame helper is closed or busy');
    // An exact, unpooled backing store transfers ownership to the worker.
    const pixels = Buffer.allocUnsafeSlow(size);
    if (pixels.write(encoded, 'base64') !== size) throw Error('Invalid restricted frame encoding');
    return new Promise((resolve, reject) => {
      this.pending = { resolve, reject };
      try { this.worker.postMessage({ buffer: pixels.buffer, width, height, verify }, [pixels.buffer]); }
      catch (error) { this.pending = null; reject(error); }
    });
  }

  async close() {
    this.closed = true;
    this.pending?.reject(Error('Restricted capture ended'));
    this.pending = null;
    await this.worker.terminate();
  }
}
