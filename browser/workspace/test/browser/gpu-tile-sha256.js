// Real stock WebGPU. Exercise the production VideoFrame reader twice on the
// same immutable frame; the independent Node oracle hashes tightly packed rows.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { chromium } from 'playwright-core';
import { testBrowserSelection } from '../support/browser-selection.js';
import { playwrightLaunchOptions } from '../../src/browser-launch.js';

const root = mkdtempSync(join(tmpdir(), 'blitzen-tile-digest-'));
const files = new Map(['resident-canvas', 'gpu-tile-sha256'].map(name =>
  ['/' + name + '.js', readFileSync(new URL('../../src/adapters/' + name + '.js', import.meta.url))]));
files.set('/frame-queue.js', readFileSync(new URL('../support/resident-frame-queue.js', import.meta.url)));
const server = createServer((req, res) => {
  if (files.has(req.url)) { res.setHeader('Content-Type', 'text/javascript'); res.end(files.get(req.url)); }
  else { res.setHeader('Content-Type', 'text/html'); res.end('<title>Blitzen digest qualification</title>'); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
let context;
try {
  const { config } = testBrowserSelection(root, { headless: false });
  context = await chromium.launchPersistentContext(root, { ...playwrightLaunchOptions(config.browser), headless: false });
  const page = await context.newPage();
  await page.goto('http://127.0.0.1:' + server.address().port);
  const results = await page.evaluate(async () => {
    const { createResidentCanvasReader } = await import('/resident-canvas.js');
    const { createGpuTileDigest } = await import('/gpu-tile-sha256.js');
    const cpu = createResidentCanvasReader(), gpu = createResidentCanvasReader(undefined, createGpuTileDigest());
    const results = [];
    try {
      async function compare(name, bytes, w, h, region, force = false) {
        const frame = new VideoFrame(bytes, { format: 'RGBA', codedWidth: w, codedHeight: h, timestamp: results.length,
          colorSpace: { primaries: 'bt709', transfer: 'iec61966-2-1', matrix: 'rgb', fullRange: true } });
        try {
          const reference = new Uint8Array(bytes.length);
          await frame.copyTo(reference, { format:'RGBA', colorSpace:'srgb', layout:[{offset:0,stride:w*4}] });
          const left = await cpu.readFrame(region, frame, force, 'rgba-upload');
          const right = await gpu.readFrame(region, frame, force, 'rgba-upload');
          results.push({ name, left, right, reference: reference.toBase64(), alphaCount: new Set(reference.filter((_, i) => i % 4 === 3)).size });
        } finally { frame.close(); }
      }
      function pattern(w,h,translucent = false) {
        const bytes = new Uint8Array(w*h*4);
        for (let i=0; i<w*h; i++) bytes.set([(i*197+3)&255, (i*17+8)&255, (i*61+42)&255, translucent?(i*127)&255:255],i*4);
        return bytes;
      }
      for (const w of [1, 13, 14, 15, 16, 17]) await compare('padding-'+w, pattern(w,1), w,1, 'width-'+w);
      const odd = pattern(67,65);
      await compare('odd-full', odd,67,65,'odd');
      await compare('unchanged',odd,67,65,'odd');
      odd[odd.length-4]^=255;
      await compare('edge-change',odd,67,65,'odd');
      await compare('forced-full',odd,67,65,'odd',true);
      const translucent=pattern(65,65,true);
      await compare('alpha',translucent,65,65,'alpha');
      await compare('alpha-unchanged',translucent,65,65,'alpha');
      return results;
    } finally { cpu.dispose(); gpu.dispose(); }
  });
  const report = [];
  for (const {name,left,right,reference,alphaCount} of results) {
    const bytes=Buffer.from(reference,'base64'), expected=[];
    for (let y=0;y<left.height;y+=left.tileSize) for (let x=0;x<left.width;x+=left.tileSize) {
      const digest=createHash('sha256'), count=Math.min(left.tileSize,left.width-x)*4;
      for (let row=y;row<Math.min(left.height,y+left.tileSize);row++) {
        const start=(row*left.width+x)*4; digest.update(bytes.subarray(start,start+count));
      }
      expected.push(digest.digest('hex'));
    }
    assert.deepEqual(left.hashes,expected,name+' CPU rows');
    assert.deepEqual(right.hashes,expected,name+' GPU words');
    assert.equal(right.tilePixelReadbackBytes,0);
    assert.equal(left.gpuMaskReadbackBytes,right.gpuMaskReadbackBytes);
    const selected = name==='unchanged' ? 0 : name==='edge-change' ? 1 : expected.length;
    assert.equal(right.rehashedTiles, selected);
    assert.equal(left.rehashedTiles, selected);
    assert.equal(right.digestReadbackBytes,selected*32,name+' digest bytes');
    assert.equal(right.mappedReadbackBytes,right.digestReadbackBytes);
    if (name.startsWith('alpha')) assert.ok(alphaCount>16,name+' nonuniform alpha');
    if (name==='forced-full') assert.equal(right.comparedOnGpu,false);
    if (name==='odd-full') assert.ok(right.digestComputePasses>1);
    report.push({name,tiles:expected.length,rehash:selected,cpuMappedBytes:left.mappedReadbackBytes,
      gpuMappedBytes:right.mappedReadbackBytes,maskBytes:right.gpuMaskReadbackBytes,
      jobUploadBytes:right.digestJobUploadBytes,passes:right.digestComputePasses,alphaCount});
  }
  const queue = await page.evaluate(async () => {
    const { sampleFrameQueue } = await import('/frame-queue.js');
    const { createResidentCanvasReader } = await import('/resident-canvas.js');
    return sampleFrameQueue(createResidentCanvasReader);
  });
  assert.deepEqual([queue.first.waits, queue.retained.waits, queue.changed.waits], [1, 0, 0]);
  assert.deepEqual([queue.first.rehashed, queue.retained.rehashed, queue.changed.rehashed], [2, 0, 1]);
  const red = Buffer.alloc(64 * 64 * 4, Buffer.from([255, 0, 0, 255]));
  const tileHash = pixels => createHash('sha256').update(pixels).digest('hex');
  const solid = tileHash(red);
  Buffer.from([0, 255, 0, 255]).copy(red, 4 * (21 * 64 + 100 - 64));
  assert.deepEqual(queue.first.hashes, [solid, solid]);
  assert.deepEqual(queue.retained.hashes, [solid, solid]);
  assert.deepEqual(queue.changed.hashes, [solid, tileHash(red)]);
  assert.deepEqual(queue.onError, { waits: 1, rejectedBeforeDrain: false, error: 'fixture mask map failed' });
  console.log(JSON.stringify({cases: report, exact:true, queue, hardwareQualified:false}));
} finally {
  await context?.close();
  await new Promise(resolve=>server.close(resolve)); rmSync(root,{recursive:true,force:true});
}
