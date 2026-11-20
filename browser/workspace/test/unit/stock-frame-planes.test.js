import assert from 'node:assert/strict';
import { canonicalRestrictedFrame, canonicalRgbaTiles } from '../../src/adapters/compositor-capture.js';
import { stockFramePlaneEvidence } from '../../src/adapters/stock-frame-planes.js';
import { supportsStockCapturePlanes } from '../../src/adapters/stock-capture-source.js';

assert.equal(await supportsStockCapturePlanes(process.pid), false);
const rect = { x: 0, y: 0, width: 128, height: 64 };
const yuv = new Uint8Array(rect.width * rect.height * 1.5);
let exports = 0;
const frame = { format: 'I420', visibleRect: rect, codedWidth:128, codedHeight:64, displayWidth:128, displayHeight:64,
  colorSpace: { matrix:'bt709', primaries:'bt709', transfer:'bt709', fullRange:false },
  allocationSize: () => yuv.length,
  copyTo: async (destination, { layout }) => { exports++; destination.set(yuv); return layout; },
  fawnRestrictedPlaneVersion() { throw Error('Stock path consulted a Fawn extension'); },
  fawnRestrictedPlaneTiles() { throw Error('Stock path consulted a Fawn extension'); },
};
const lease = {};
const sample = (f = frame, state, grant = lease, supported = true) => stockFramePlaneEvidence(f,128,64,64,state,grant,supported);
const initial = await sample();
assert.deepEqual(initial.plan, [1,0,null]);
assert.deepEqual((await sample(frame, initial)).plan, [1,1,null]);
assert.deepEqual((await sample(frame, initial, {})).plan, [1,0,null]);
assert.equal(exports,3);
for (const f of [{ ...frame, format:'NV12' }, { ...frame, visibleRect:{...rect,x:2} },
  { ...frame, displayWidth:126 }, { ...frame, flip:true }, { ...frame, rotation:90 },
  { ...frame, colorSpace:{...frame.colorSpace,fullRange:true} }]) {
  assert.deepEqual((await sample(f, initial)).plan,[]);
}
const unsupported = await sample(frame, initial, lease, false);
assert.equal(exports,3);
assert.equal(unsupported.exportedBytes,0);
const full = new Uint8Array(rect.width * rect.height * 4);
const fallback = await canonicalRestrictedFrame(frame,128,64,64,null,async () => full,canonicalRgbaTiles,unsupported);
assert.equal(fallback.copiedBytes,full.length);
const invalidLayout = await sample({ ...frame, copyTo: async () => [] }, initial);
assert.deepEqual(invalidLayout.plan, []);
assert.equal(invalidLayout.exportedBytes,yuv.length);
console.log('ok: stock eligibility, capture lease, fresh SHA-256 fallback and invalid plane layout');
