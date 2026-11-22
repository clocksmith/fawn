import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { loadServiceConfig } from '../../src/config.js';
import { openFakeService, createClock, ORIGIN } from '../support/harness.js';

const pixels = Buffer.from([200, 0, 0, 255]);
const baselineDigest = createHash('sha256').update(pixels).digest('hex');
const evidence = { evidenceId: 'browser-only', documentId: 'document', surfaceId: 'surface', frameRevision: '8',
  deviceScaleFactor: 1, pixelWidth: 1, pixelHeight: 1, rgba: pixels.toString('base64'), baselineDigest };
const rect = { x: 0, y: 0, width: 1, height: 1 };
let wireResult = { admitted: false, reason: 'stale-compositor-revision' };
const wire = { async send(method) {
  if (method === 'Input.captureVisualEvidence') return evidence;
  if (method === 'Page.getFrameTree') return { frameTree: { frame: { loaderId: 'load' } } };
  assert.equal(method, 'Input.dispatchReviewedClick');
  if (wireResult instanceof Error) throw wireResult;
  return wireResult;
} };
const cdp = createCdpAdapter({});
cdp._workspaces.set('work', { cdp: wire, pages: new Map([['main', { sessionId: 'session' }]]) });
const frame = { loaderId: 'load', sessionId: 'capture', surfaceId: 'selected', bindingDigest: 'bound',
  binding: { deviceScaleFactor: 1, viewportRect: rect } };
const bitmap = { format: 'compositor-element-canonical-srgb-rgba8-v2', width: 1, height: 1, tileSize: 64, hashes: [baselineDigest] };
const snap = { frameEvidence: { '#name': frame }, regionBitmaps: { '#name': bitmap } };
const prepare = () => cdp.prepareReviewedClick('work', 'main', '#name', snap);
const first = await prepare();
bitmap.hashes = ['different-granted-pixels'];
await assert.rejects(prepare(), /pixels differ/);
assert.equal((await cdp.executeReviewedClick('work', 'main', first.binding.id)).admitted, false);
bitmap.hashes = [baselineDigest];

const root = mkdtempSync(join(tmpdir(), 'fawn-reviewed-input-'));
const config = structuredClone(loadServiceConfig());
config.browser.headless = false;
config.observation.compositorCapture = true;
const { service, adapter } = await openFakeService({ stateDir: root, receiptsPath: join(root, 'effects'),
  clock: createClock(), config });
try {
  const { adminCredential } = await service.createWorkspace({ workspaceId: 'work', scopedReads: true });
  const admin = { workspaceId: 'work', adminCredential };
  const owner = { workspaceId: 'work', ...await service.assignOwner({ ...admin, ownerId: 'person' }) };
  await service.grantAuthorization({ ...admin, grantId: 'pixels', ownerId: 'person', intents: ['review'], actionKinds: ['click'],
    targetOrigins: [ORIGIN], pageIds: ['main'], readRegions: ['#name'] });
  await adapter.openWorkspace('work');
  adapter._page('work').url = ORIGIN + '/form';
  const snapshot = adapter.snapshot.bind(adapter);
  adapter.snapshot = async (id, read) => {
    const data = await snapshot(id, read);
    if (read.capture) Object.assign(data, { frameEvidence: { '#name': { ...frame,
      grantId: read.capture.grantId, grantRevision: read.capture.grantRevision } },
      regionBitmaps: snap.regionBitmaps, regionDigests: { '#name': 'same' } });
    return data;
  };
  adapter.prepareReviewedClick = cdp.prepareReviewedClick.bind(cdp);
  adapter.executeReviewedClick = cdp.executeReviewedClick.bind(cdp);
  const observe = () => service.observe({ ...owner, pageId: 'main', regions: ['#name'], reviewedClick: true,
    compositorRegion: '#name', compositorSession: 'start' });
  const act = (obs, actionId, handle = 'hname') => service.submitAction({ ...owner, pageId: 'main', actionId, authorizedIntent: 'review',
    kind: 'click', target: { handle }, observationVersion: obs.observationVersion, requireReviewedClick: true }).then(x => x.result);
  const plain = await service.observe(owner);
  await assert.rejects(act(plain, 'unreviewed'), { code: 'stale-observation' });
  const reviewed = await observe();
  assert.equal(reviewed.reviewedRgba, evidence.rgba);
  await assert.rejects(act(reviewed, 'wrong-target', 'hemail'), /only a click on that selected region|unknown-target/);
  wireResult = { admitted: false, reason: 'stale-compositor-revision' };
  const refused = await act(reviewed, 'late-change');
  assert.equal(refused.outcome, 'failed'); assert.equal(refused.outcomeBasis, wireResult.reason);
  wireResult = { admitted: true };
  const accepted = await act(await observe(), 'fresh');
  assert.equal(accepted.outcome, 'completed'); assert.equal(accepted.outcomeBasis, 'reviewed-input-admitted');
  wireResult = Error('connection lost');
  assert.equal((await act(await observe(), 'uncertain')).outcome, 'unknown');
  assert.equal(adapter.executions.length, 0, 'ordinary input never substituted for a native review');
} finally { await service.close(); rmSync(root, { recursive: true, force: true }); }
console.log('ok: reviewed pixels, session binding, native-only routing, stale refusal and lost acknowledgment');
