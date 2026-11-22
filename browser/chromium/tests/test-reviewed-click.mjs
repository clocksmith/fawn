import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { reviewedClicks } from '../scripts/reviewed-click.mjs';

const pixels = Buffer.from([200, 0, 0, 255, 200, 0, 0, 255]);
const baselineDigest = createHash('sha256').update(pixels).digest('hex');
const evidence = { evidenceId: 'browser-only', documentId: 'document', surfaceId: 'surface', frameRevision: '8',
  deviceScaleFactor: 1, pixelWidth: 2, pixelHeight: 1, rgba: pixels.toString('base64'), baselineDigest };
const rect = { x: 0, y: 0, width: 2, height: 1 };
const point = { x: 1, y: 0 };
const calls = [];
let wireEvidence = evidence, wireResult = { admitted: false, delivered: false, reason: 'stale-compositor-revision' };
const wire = { async send(method, args) {
  calls.push({ method, args });
  if (method === 'Input.captureVisualEvidence') return wireEvidence;
  assert.equal(method, 'Input.dispatchReviewedClick');
  if (wireResult instanceof Error) throw wireResult;
  return wireResult;
} };
const client = reviewedClicks(wire);
await assert.rejects(client.capture(rect), /click point/);
await assert.rejects(client.capture(rect, { x: 2, y: 0 }), /click point/);
const image = await client.capture(rect, point);
assert.deepEqual(calls.at(-1).args, { ...rect, targetX: 1, targetY: 0 });
assert.deepEqual(image.point, point);
assert.ok(!JSON.stringify(image).includes('browser-only'));
await assert.rejects(reviewedClicks(wire).click(image), /again in this session/);
await assert.rejects(client.click(image, { x: 0, y: 0 }), /reviewed point/);
assert.deepEqual(await client.click(image), wireResult);
await assert.rejects(client.click(image), /again in this session/);
// Admitted but cancelled because the recipient changed is a determinate result.
wireResult = { admitted: true, delivered: false, reason: 'target-changed' };
assert.deepEqual(await client.click(await client.capture(rect, point), point), wireResult);
// Delivery without admission is incoherent.
wireResult = { admitted: false, delivered: true, reason: 'delivered' };
await assert.rejects(client.click(await client.capture(rect, point)), /indeterminate/);
wireResult = Error('lost transport');
const lost = await client.capture(rect, point);
await assert.rejects(client.click(lost), /lost transport/);
await assert.rejects(client.click(lost), /again in this session/);
wireEvidence = { ...evidence, baselineDigest: '0'.repeat(64) };
await assert.rejects(client.capture(rect, point), /do not match/);

assert.equal(calls.filter(call => call.method === 'Input.dispatchReviewedClick').length, 4);
console.log('reviewed-click client: ok');
