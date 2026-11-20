// The pinned Chromium tab capturer allocates I420 from SharedMemoryVideoFramePool.
// Arbitrary GPU-backed WebCodecs frames may also report I420. Restrict sparse RGBA
// reuse to the known capture implementation. Other browsers still get full RGBA.
import { createHash } from 'node:crypto';
import { open } from 'node:fs/promises';
const expected = '01a0f7b8dd41057bb5bf3f48a598573976059e55eda36053f2173cdc907cab59';
let lastImage;

export async function supportsStockCapturePlanes(browserPid) {
  if (process.platform !== 'linux' || !Number.isSafeInteger(browserPid) || browserPid <= 0) return false;
  let file;
  try {
    file = await open('/proc/' + browserPid + '/exe');
    const stat = await file.stat({ bigint: true });
    if (!stat.isFile() || stat.size !== 534164032n) return false;
    const key = [stat.dev, stat.ino, stat.size, stat.ctimeNs].join('/');
    if (lastImage?.key === key) return lastImage.supported;
    const hash = createHash('sha256');
    for await (const chunk of file.createReadStream({ autoClose: false })) hash.update(chunk);
    const supported = hash.digest('hex') === expected;
    lastImage = { key, supported };
    return supported;
  } catch { return false; }
  finally { await file?.close(); }
}
