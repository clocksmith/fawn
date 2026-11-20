// Browser conversion oracle. The mask here is supplied by the test; the pinned
// renderer test separately exercises the real capture-bound native producer.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { chromium } from 'playwright-core';
import { canonicalRestrictedFrame, canonicalRgbaTiles } from '../../src/adapters/compositor-capture.js';

if (!process.env.BLITZEN_STOCK_FRAME_BINARY) {
  console.log('skip: a stock Chromium binary is required for the WebCodecs boundary test');
  process.exit(0);
}
const server = createServer((_req, res) => res.end('<!doctype html><title>WebCodecs canonical pixels</title>'));
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let browser;
try {
  browser = await chromium.launch({ executablePath: process.env.BLITZEN_STOCK_FRAME_BINARY, args: ['--disable-gpu'] });
  const page = await browser.newPage();
  await page.goto('http://127.0.0.1:' + server.address().port);
  await page.addScriptTag({ content: canonicalRgbaTiles.toString() + '\n' + canonicalRestrictedFrame.toString() });
  const rows = await page.evaluate(async () => {
    const w = 512, h = 256, t = 64, luma = w * h;
    const raw = Uint8Array.from({ length: luma * 1.5 }, (_, i) => (i * 19 + Math.floor(i / 7)) & 255);
    let prior = null, serial = 0, oldHashes;
    const results = [];
    const copy = async (frame, width, height, rect = { x: 0, y: 0, width, height }) => {
      const bytes = new Uint8Array(rect.width * rect.height * 4);
      await frame.copyTo(bytes, { rect, format: 'RGBA', colorSpace: 'srgb', layout: [{ offset: 0, stride: rect.width * 4 }] });
      return bytes;
    };
    for (const [kind, offset, dirty] of [
      ['baseline', -1, []], ['luma', 82 * w + 83, [9]],
      ['U at edge', luma + 31 * (w / 2) + 31, [0, 1, 8, 9]],
      ['V at edge', luma + luma / 4 + 32 * (w / 2) + 32, [0, 1, 8, 9]],
      ['unchanged', -1, []], ['new lease', -1, []],
    ]) {
      if (offset >= 0) raw[offset] ^= 127;
      const frame = new VideoFrame(raw, { format: 'I420', codedWidth: w, codedHeight: h, timestamp: 0,
        colorSpace: { primaries: 'bt709', transfer: 'bt709', matrix: 'bt709', fullRange: false } });
      try {
        const previous = serial;
        if (!serial || offset >= 0) ++serial;
        // Never installed by Blitzen or emitted as a trusted browser grant.
        Object.defineProperty(frame, 'fawnRestrictedPlaneTiles', { value: () => [
          serial, kind === 'new lease' ? 0 : previous, 0, ...dirty,
        ] });
        const fresh = await canonicalRestrictedFrame(frame, w, h, t, prior, copy, canonicalRgbaTiles);
        const reference = await canonicalRgbaTiles(await copy(frame, w, h), w, h, t);
        const exact = fresh.record.hashes.every((hash, i) => hash === reference.record.hashes[i]);
        const actualChanged = reference.record.hashes.flatMap((hash, i) => oldHashes && hash !== oldHashes[i] ? [i] : []);
        results.push({ kind, exact, actualChanged, copiedBytes: fresh.copiedBytes,
          hashedBytes: fresh.hashedBytes, tilesHashed: fresh.tilesHashed });
        prior = fresh;
        oldHashes = reference.record.hashes;
      } finally { frame.close(); }
    }
    return results;
  });
  assert.ok(rows.every(row => row.exact));
  assert.deepEqual(rows.map(row => row.copiedBytes), [524288, 16384, 65536, 65536, 0, 524288]);
  assert.ok(rows.slice(1, 4).every(row => row.actualChanged.length));
  console.log(JSON.stringify({ chromium: browser.version(), rows }));
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
