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
