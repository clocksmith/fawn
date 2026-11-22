import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { reviewedClicks } from '../scripts/reviewed-click.mjs';

const pixels = Buffer.from([200, 0, 0, 255]);
const baselineDigest = createHash('sha256').update(pixels).digest('hex');
const evidence = { evidenceId: 'browser-only', documentId: 'document', surfaceId: 'surface', frameRevision: '8',
  deviceScaleFactor: 1, pixelWidth: 1, pixelHeight: 1, rgba: pixels.toString('base64'), baselineDigest };
const rect = { x: 0, y: 0, width: 1, height: 1 };
const calls = [];
let wireEvidence = evidence, wireResult = { admitted: false, reason: 'stale-compositor-revision' };
const wire = { async send(method, args) {
  calls.push({ method, args });
  if (method === 'Input.captureVisualEvidence') return wireEvidence;
  if (method === 'Page.getFrameTree') return { frameTree: { frame: { loaderId: 'load' } } };
  assert.equal(method, 'Input.dispatchReviewedClick');
  if (wireResult instanceof Error) throw wireResult;
  return wireResult;
} };
const client = reviewedClicks(wire);
const image = await client.capture(rect);
assert.ok(!JSON.stringify(image).includes('browser-only'));
await assert.rejects(reviewedClicks(wire).click(image, rect), /again in this session/);
await assert.rejects(client.click(image, { x: 1, y: 0 }), /inside/);
assert.deepEqual(await client.click(image, rect), wireResult);
await assert.rejects(client.click(image, rect), /again in this session/);
wireResult = Error('lost transport');
const lost = await client.capture(rect);
await assert.rejects(client.click(lost, rect), /lost transport/);
await assert.rejects(client.click(lost, rect), /again in this session/);
wireEvidence = { ...evidence, baselineDigest: '0'.repeat(64) };
await assert.rejects(client.capture(rect), /do not match/);
wireEvidence = evidence;

assert.equal(calls.filter(call => call.method === 'Input.dispatchReviewedClick').length, 2);
