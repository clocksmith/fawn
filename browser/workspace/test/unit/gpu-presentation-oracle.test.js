import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { assertSolidPixels, decodePng, extractRenderShaders } from '../support/gpu-presentation-oracle.js';

// Independent Pillow RGBA encodings, not produced by the decoder under test.
const samples = [
  ['iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFklEQVR4nGP8z8DwnwEPYMInOXwUAAASWwIOH0pJXQAAAABJRU5ErkJggg==', [255, 0, 0, 255]],
  ['iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFklEQVR4nGP8z/D/PwMewIRPcvgoAAAWZQMNpyQ17QAAAABJRU5ErkJggg==', [255, 0, 255, 255]],
  ['iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFklEQVR4nGNk+M/wnwEPYMInOXwUAAARXAIO3AvTcAAAAABJRU5ErkJggg==', [0, 255, 0, 255]],
  ['iVBORw0KGgoAAAANSUhEUgAAAAgAAAAICAYAAADED76LAAAAFklEQVR4nGNkYPj/nwEPYMInOXwUAAAQXQIOZWZ6QQAAAABJRU5ErkJggg==', [0, 0, 255, 255]],
];
for (const [encoded, expected] of samples) {
  const png = Buffer.from(encoded, 'base64');
  assert.equal(assertSolidPixels(png, expected).pixelCount, 64);
  assert.throws(() => assertSolidPixels(png, [1, 2, 3, 255]), /compositor pixel/);
  const corrupt = Buffer.from(png);
  corrupt[45] ^= 1;
  assert.throws(() => decodePng(corrupt), /CRC/);
  assert.throws(() => decodePng(png.subarray(0, png.length - 1)), /truncated/);
}
assert.throws(() => assertSolidPixels(Buffer.from(samples[3][0], 'base64'), [255, 0, 0, 255]), /compositor pixel 0/,
  'a blue GPU endpoint must reject a red/blue channel swap');
const fixture = readFileSync(new URL('../fixtures/gpu-composition.html', import.meta.url), 'utf8');
const shaders = extractRenderShaders(fixture);
assert.match(shaders.vertex, /@vertex/);
assert.match(shaders.fragment, /@fragment/);
assert.ok(fixture.includes('`' + shaders.vertex + '`'));
assert.ok(fixture.includes('`' + shaders.fragment + '`'));
assert.throws(() => extractRenderShaders(''), /one unchanged vertex/);
assert.throws(() => extractRenderShaders(fixture + fixture), /one unchanged vertex/);
assert.throws(() => extractRenderShaders(fixture.replace('@vertex', '@vertex ${unsafe}')), /interpretation/);
console.log('ok: independent PNG references, corrupt/wrong-pixel rejection and exact original shader extraction');
