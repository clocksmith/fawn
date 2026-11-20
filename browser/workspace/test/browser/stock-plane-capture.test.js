// Actual permission-restricted frames and controlled WebCodecs I420 inputs are
// checked against a full RGBA read of the same wrapper. No Fawn APIs are used.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtemp, rm } from 'node:fs/promises';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { chromium } from 'playwright-core';
import { canonicalRgbaTiles, canonicalRestrictedFrame } from '../../src/adapters/compositor-capture.js';
import { stockFramePlaneEvidence } from '../../src/adapters/stock-frame-planes.js';
import { supportsStockCapturePlanes } from '../../src/adapters/stock-capture-source.js';

if (process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1') {
  console.log('skip: a disposable display and an explicitly selected tab are required'); process.exit(0);
}

function observationReader() {
  let prior, previousPlanes, priorFull, oldHashes, lease = {};
  const copy = async (frame, width, height, rect = { x: 0, y: 0, width, height }) => {
    const bytes = new Uint8Array(rect.width * rect.height * 4);
    await frame.copyTo(bytes, { rect, format: 'RGBA', colorSpace: 'srgb', layout: [{ offset: 0, stride: 4 * rect.width }] });
    return bytes;
  };
  return {
    replaceGrant() { lease = {}; prior = null; priorFull = null; },
    async sample(frame, name) {
      const w = frame.displayWidth, h = frame.displayHeight, tile = 64;
      if ('fawnRestrictedPlaneTiles' in frame || 'fawnRestrictedPlaneVersion' in frame) throw Error('Stock Chromium required');
      const pixels = await copy(frame, w, h);
      const reference = await canonicalRgbaTiles(pixels, w, h, tile, priorFull);
      priorFull = reference.record;
      const evidence = await stockFramePlaneEvidence(frame, w, h, tile, previousPlanes, lease, true);
      const result = await canonicalRestrictedFrame(frame, w, h, tile, prior, copy, canonicalRgbaTiles, evidence);
      const exact = result.record.hashes.every((hash, i) => hash === reference.record.hashes[i]);
      const changed = reference.record.hashes.flatMap((hash, i) => oldHashes && hash !== oldHashes[i] ? [i] : []);
      prior = result; oldHashes = reference.record.hashes; previousPlanes = evidence;
      return { name, exact, width: w, height: h, format: frame.format, rgba: [...pixels.slice(0, 4)],
        changed, candidates: evidence.plan[1] ? evidence.plan.slice(3) : null,
        planeExportBytes: evidence.exportedBytes, rgbaCopyBytes: result.copiedBytes, rgbaHashBytes: result.hashedBytes,
        fullCanonicalCopyBytes: pixels.length, fullCanonicalHashBytes: reference.hashedBytes };
    },
  };
}

async function controlledPlanes() {
  const w = 512, h = 256, luma = w*h, state = observationReader(), rows = [];
  const bytes = Uint8Array.from({length: luma*1.5}, (_, i) => (19*i + Math.floor(i/7)) & 255);
  for (const [name, offset] of [['base',-1],['luma',82*w+83],['U boundary',luma + 31*(w/2)+31],
    ['V boundary',luma*1.25 + 32*(w/2)+32],['unchanged',-1],['new grant',-1], ['recorded baseline',-1], ['recorded collision',-1]]) {
    if (offset >= 0) bytes[offset] ^= 127;
    if (name === 'recorded baseline' || name === 'recorded collision') {
      bytes.fill(40, 0, luma);
      bytes.fill(name === 'recorded baseline' ? 115 : 114, luma, luma + luma / 4);
      bytes.fill(176, luma + luma / 4);
    }
    if (name === 'new grant') state.replaceGrant();
    const frame = new VideoFrame(bytes, { format:'I420', codedWidth:w, codedHeight:h, timestamp:0,
      colorSpace: { primaries:'bt709', transfer:'bt709', matrix:'bt709', fullRange:false } });
    try { rows.push(await state.sample(frame, name)); } finally { frame.close(); }
  }
  return rows;
}

async function restrictedSamples(replacement) {
  const canvas = document.querySelector('canvas');
  const ctx = canvas.getContext('2d', { alpha: false });
  const state = observationReader(), rows = [];
  const paint = (color, x=0, y=0, width=512, height=256) => { ctx.fillStyle = color; ctx.fillRect(x,y,width,height); };
  paint('rgb(113 5 0)');
  const target = await RestrictionTarget.fromElement(canvas);
  const stream = await navigator.mediaDevices.getDisplayMedia({ preferCurrentTab:true, surfaceSwitching:'exclude', audio:false,
    video:{ displaySurface:'browser', cursor:'never' } });
  const track = stream.getVideoTracks()[0];
  let reader;
  try {
    if (track.getSettings().displaySurface !== 'browser') throw Error('Tab capture required');
    await track.restrictTo(target);
    await track.applyConstraints({ frameRate:{ min:5, max:15 } });
    reader = new MediaStreamTrackProcessor({track,maxBufferSize:1}).readable.getReader();
    for (const name of replacement ? ['replacement capture'] : ['base','unchanged','luma patch','crossing patch','restored','collision']) {
      if (name === 'luma patch') paint('lime',82,82,4,4);
      if (name === 'crossing patch') paint('blue',62,62,4,4);
      if (name === 'restored') paint('rgb(113 5 0)');
      if (name === 'collision') paint('rgb(114 6 0)');
      await new Promise(resolve => setTimeout(resolve, 350));
      for (let n=0;n<2;n++) (await reader.read()).value.close();
      const {value:frame}=await reader.read();
      try { rows.push(await state.sample(frame,name)); } finally { frame.close(); }
    }
  } finally { stream.getTracks().forEach(t=>t.stop()); await reader?.cancel().catch(()=>{}); }
  return rows;
}

const server = createServer((_req,res)=>res.end('<!doctype html><title>Restricted plane comparison</title><style>*{margin:0}canvas{display:block;isolation:isolate;position:relative}</style><canvas width="512" height="256"></canvas>'));
const dir = await mkdtemp(join(tmpdir(),'stock-plane-check-'));
let browser;
try {
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  browser = await chromium.launchPersistentContext(dir,{executablePath:process.env.BLITZEN_STOCK_FRAME_BINARY,headless:false,viewport:{width:512,height:520},deviceScaleFactor:1});
  const control = await browser.browser().newBrowserCDPSession();
  const processes = await control.send('SystemInfo.getProcessInfo');
  assert.equal(await supportsStockCapturePlanes(processes.processInfo.find(r=>r.type==='browser').id),true);
  await control.detach();
  const page=browser.pages()[0]; await page.goto('http://127.0.0.1:'+server.address().port);
  const cdp=await browser.newCDPSession(page);
  const {frameTree}=await cdp.send('Page.getFrameTree');
  const {executionContextId}=await cdp.send('Page.createIsolatedWorld',{frameId:frameTree.frame.id,worldName:'Stock plane comparison'});
  async function evaluate(expression) {
    const r=await cdp.send('Runtime.evaluate',{contextId:executionContextId,expression,returnByValue:true,awaitPromise:true,userGesture:true});
    if(r.exceptionDetails) throw Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
    return r.result.value;
  }
  await evaluate([canonicalRgbaTiles,canonicalRestrictedFrame,stockFramePlaneEvidence,observationReader,controlledPlanes,restrictedSamples].map(fn=>fn.toString()).join('\n'));
  const controlled=await evaluate('controlledPlanes()'),captured=await evaluate('restrictedSamples(false)'),replacement=await evaluate('restrictedSamples(true)');
  const all=[...controlled,...captured,...replacement];
  assert.ok(all.every(r=>r.exact && r.planeExportBytes===196608));
  assert.deepEqual(controlled.map(r=>r.rgbaCopyBytes),[524288,16384,65536,65536,0,524288,524288,524288]);
  assert.deepEqual(controlled[6].rgba,[114,5,2,255]);
  assert.deepEqual(controlled[7].rgba,[114,5,0,255]);
  assert.equal(controlled[7].changed.length,32);
  assert.equal(captured[1].rgbaCopyBytes,0);
  assert.ok(captured[2].rgbaCopyBytes>0 && captured[2].rgbaCopyBytes<524288);
  assert.ok(captured[3].changed.length >= 2 && captured[3].rgbaCopyBytes < 524288);
  // A stock software compositor can quantize these source colors together before capture.
  // The recorded native collision above is distinct on every canonical tile.
  assert.ok(captured[5].changed.length === 0 || captured[5].changed.length === 32);
  assert.equal(replacement[0].rgbaCopyBytes,524288);
  console.log(JSON.stringify({browser:await cdp.send('Browser.getVersion'),controlled,captured,replacement}));
} finally { await browser?.close(); await new Promise(resolve=>server.close(resolve)); await rm(dir,{recursive:true,force:true}); }
