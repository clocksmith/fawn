// Compare independent canvas-review receipts. Timings and hardware attribution
// belong to the separate physical comparison.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const paths = process.argv.slice(2);
assert.equal(paths.length, 2, 'Supply the CPU and WebGPU canvas-review receipt files');
const [cpu, gpu] = paths.map(path => JSON.parse(readFileSync(path, 'utf8')));
for (const [receipt, mode] of [[cpu, 'cpu'], [gpu, 'webgpu']]) {
  assert.equal(receipt.digestMode, mode);
  assert.equal(receipt.compositorTransport, 'external-texture');
  assert.ok(receipt.imageCorrect && receipt.verified && receipt.reviewedBitmap?.hashes.length);
  assert.equal(receipt.committedRevision, 'B-r2');
  assert.equal(receipt.knownEffects, 1);
}
assert.deepEqual(cpu.reviewedBitmap, gpu.reviewedBitmap, 'source dimensions and all reviewed tile fingerprints');
assert.deepEqual(cpu.indicator, gpu.indicator, 'localized target');
assert.equal(gpu.tilePixelReadbackBytes, 0);
assert.ok(gpu.rehashedTiles > 0);
assert.equal(gpu.digestReadbackBytes, gpu.rehashedTiles * 32);
assert.equal(gpu.digestReadbackBytes, gpu.mappedTileReadbackBytes);
const fields = ['mappedTileReadbackBytes', 'tilePixelReadbackBytes', 'digestReadbackBytes', 'gpuMaskReadbackBytes',
  'mappedTileAndMaskReadbacks', 'digestJobUploadBytes', 'digestComputePasses', 'rehashedTiles', 'knownEffects',
  'applicationHttpRequests', 'plannerCalls', 'modelSpendUsd'];
const selected = arm => Object.fromEntries(fields.map(name => [name, arm[name] ?? null]));
console.log(JSON.stringify({ equalReviewedHashes: true, equalTarget: true, verifiedRevision: gpu.committedRevision,
  observedDimensions: [gpu.reviewedBitmap.width, gpu.reviewedBitmap.height], cpu: selected(cpu), webgpu: selected(gpu),
  latencyQualified: false, hardwareQualified: false }, null, 2));
