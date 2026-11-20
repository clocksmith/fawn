import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { canonicalRgbaTiles } from '../../src/adapters/compositor-capture.js';

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const width = 3, height = 2, tile = 2;
const bytes = Uint8Array.from({ length: width * height * 4 }, (_, i) => (7 * i) & 255);
const first = await canonicalRgbaTiles(bytes, width, height, tile);
assert.deepEqual(first.record.hashes, [
  hash(Buffer.concat([bytes.slice(0, 8), bytes.slice(12, 20)])),
  hash(Buffer.concat([bytes.slice(8, 12), bytes.slice(20, 24)])),
]);
assert.equal(first.record.frameDigest, hash(bytes));
assert.equal(first.hashedBytes, bytes.length * 2);
const identical = await canonicalRgbaTiles(bytes.slice(), width, height, tile, first.record);
assert.equal(identical.record, first.record);
assert.equal(identical.hashedBytes, bytes.length);
assert.equal(identical.tilesHashed, 0);
// A one-byte alpha change on the right edge leaves the first tile intact.
const changed = bytes.slice();
changed[23] ^= 1;
const second = await canonicalRgbaTiles(changed, width, height, tile, first.record);
assert.notEqual(second.record.frameDigest, first.record.frameDigest);
assert.equal(second.record.hashes[0], first.record.hashes[0]);
assert.notEqual(second.record.hashes[1], first.record.hashes[1]);
const reshaped = await canonicalRgbaTiles(bytes, height, width, tile, first.record);
assert.equal(reshaped.record.frameDigest, first.record.frameDigest);
assert.notEqual(reshaped.record, first.record);
const single = await canonicalRgbaTiles(bytes, width, height, 64);
assert.deepEqual(single.record.hashes, [hash(bytes)]);
assert.equal(single.hashedBytes, bytes.length);
await assert.rejects(canonicalRgbaTiles(bytes, 4096, 1, tile), /geometry/);
console.log('ok: canonical capture reuses only equal RGBA at equal geometry and packs edge tiles without padding');

const { canonicalRestrictedFrame } = await import('../../src/adapters/compositor-capture.js');
const frame = { fawnRestrictedPlaneVersion: () => serial };
let serial = 1, copies = 0;
const read = async () => { ++copies; return bytes; };
const compare = (prior, sample = frame) => canonicalRestrictedFrame(sample,width,height,tile,prior,read,canonicalRgbaTiles);
const established = await compare(null);
assert.equal(established.usedNativeEquality,false);
assert.equal((await compare(established)).usedNativeEquality,true);
assert.equal(copies,1);
serial = 2;
assert.equal((await compare(established)).usedNativeEquality,false);
serial = 0;
const unknown = await compare(established);
assert.equal(unknown.usedNativeEquality,false);
serial = 1;
assert.equal((await compare(unknown)).usedNativeEquality,false);
assert.equal((await compare(established,{})).usedNativeEquality,false);
assert.equal(copies,5);
console.log('ok: native equality reuses only a successfully canonicalized serial from this reader; unsupported and changed samples read RGBA');

const w = 512, h = 256, t = 64;
const raw = new Uint8Array(w * h * 4);
for (let i = 0; i < raw.length; i++) raw[i] = i * 37;
let lease = {}, version = 1, differences = [], copySizes = [];
function sample() {
  const current = lease;
  return { current, fawnRestrictedPlaneTiles(previous, previousVersion, tileSize) {
    assert.equal(tileSize, t);
    return [version, previous?.current === current && previousVersion > 0 ? previousVersion : 0,
      version === previousVersion ? 0 : w, ...differences];
  } };
}
async function readTile(_frame, _width, _height, rect = { x: 0, y: 0, width: w, height: h }) {
  const result = new Uint8Array(rect.width * rect.height * 4);
  for (let y = 0; y < rect.height; y++) {
    const start = ((rect.y + y) * w + rect.x) * 4;
    result.set(raw.subarray(start, start + rect.width * 4), y * rect.width * 4);
  }
  copySizes.push(result.length);
  return result;
}
const observe = prior => canonicalRestrictedFrame(sample(), w, h, t, prior, readTile, canonicalRgbaTiles);
let last = await observe(null);
assert.equal(last.copiedBytes, 524288);
raw[(83 + 82 * w) * 4 + 1] ^= 1;
version++;
differences = [9];
last = await observe(last);
assert.equal(last.usedNativeTiles, true);
assert.equal(last.record.frameDigest, null);
assert.deepEqual(last.record.hashes, (await canonicalRgbaTiles(raw, w, h, t)).record.hashes);
assert.equal(last.hashedBytes, 16384);
assert.equal(last.copiedBytes, 16384);
differences = [];
last = await observe(last);
assert.equal(last.usedNativeEquality, true);
assert.equal(last.copiedBytes, 0);
assert.deepEqual(copySizes, [524288, 16384]);
lease = {};
last = await observe(last);
assert.equal(last.usedNativeEquality, false);
assert.equal(last.copiedBytes, raw.length);
version++;
differences = [9, 9];
last = await observe(last);
assert.equal(last.usedNativeTiles, false);
assert.equal(last.copiedBytes, raw.length);
version++;
differences = [999];
assert.equal((await observe(last)).copiedBytes, raw.length);
console.log('ok: partial lease-bound hashes equal full canonical reference; new lease and malformed plans reread the full frame');

differences = [8, 9];
let successfulTiles = 0;
const fallback = await canonicalRestrictedFrame(sample(), w, h, t, last, async (...args) => {
  if (args[3] && successfulTiles++ > 0) throw Error('Unsupported edge crop');
  return readTile(...args);
}, canonicalRgbaTiles);
assert.equal(fallback.rejectedTileReads, 1);
assert.equal(fallback.copiedBytes, 16384 + w * h * 4);
assert.deepEqual(fallback.record.hashes, (await canonicalRgbaTiles(raw, w, h, t)).record.hashes);
