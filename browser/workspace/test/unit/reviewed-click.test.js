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
let wireResult = { admitted: false, delivered: false, reason: 'stale-compositor-revision' };
// What the application does when the click lands; the result tests script it.
let onClick = () => {};
const sessions = [];
const wire = { async send(method, args, sessionId) {
  // Each review runs in its own browser session on the page's target.
  if (method === 'Target.attachToTarget') { sessions.push(`review-${sessions.length}`); return { sessionId: sessions.at(-1) }; }
  if (method === 'Target.detachFromTarget') return {};
  assert.match(sessionId, /^review-/, `${method} must use the review's own session`);
  if (method === 'Runtime.evaluate') return { result: { value: '1x1' } };
  if (method === 'Input.getVisualEvidenceStatus') return { holds: true, reason: 'holds', verifiedRevision: '8' };
  if (method === 'Input.captureVisualEvidence') return evidence;
  if (method === 'Page.getFrameTree') return { frameTree: { frame: { loaderId: 'load' } } };
  assert.equal(method, 'Input.dispatchReviewedClick');
  if (wireResult instanceof Error) throw wireResult;
  if (wireResult.delivered) onClick();
  return wireResult;
} };
const cdp = createCdpAdapter({});
cdp._workspaces.set('work', { cdp: wire, pages: new Map([['main', { sessionId: 'session', targetId: 'target' }]]) });
const frame = { loaderId: 'load', grantId: 'pixels', sessionId: 'capture', surfaceId: 'selected', bindingDigest: 'bound',
  binding: { deviceScaleFactor: 1, viewportRect: rect } };
const bitmap = { format: 'compositor-element-canonical-srgb-rgba8-v2', width: 1, height: 1, tileSize: 64, hashes: [baselineDigest] };
const snap = { frameEvidence: { '#name': frame }, regionBitmaps: { '#name': bitmap } };
const prepare = () => cdp.prepareReviewedClick('work', 'main', '#name', snap);
const first = await prepare();
bitmap.hashes = ['different-granted-pixels'];
await assert.rejects(prepare(), /pixels differ/);
assert.equal((await cdp.executeReviewedClick('work', 'main', first.binding.id)).admitted, false);
bitmap.hashes = [baselineDigest];
const revoked = await prepare();
// Reviews in other slots stay open beside it and report their own status.
const beside = await cdp.prepareReviewedClick('work', 'main', '#name', snap, undefined, 'item-2');
assert.equal((await cdp.reviewedClickStatus('work', 'main', revoked.binding.id)).holds, true);
assert.equal((await cdp.reviewedClickStatus('work', 'main', beside.binding.id)).holds, true);
assert.notEqual(revoked.binding.id, beside.binding.id);
cdp.invalidateReviewedClicks('work', 'pixels');
assert.equal((await cdp.executeReviewedClick('work', 'main', revoked.binding.id)).reason, 'review-expired');
assert.equal((await cdp.reviewedClickStatus('work', 'main', beside.binding.id)).reason, 'review-expired');

const root = mkdtempSync(join(tmpdir(), 'fawn-reviewed-input-'));
const config = structuredClone(loadServiceConfig());
config.browser.headless = false;
config.observation.compositorCapture = true;
const clock = createClock();
const { service, adapter } = await openFakeService({ stateDir: root, receiptsPath: join(root, 'effects'), clock, config });
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
    if (read.capture) Object.assign(data, { frameEvidence: { [read.capture.selector]: { ...frame,
      grantId: read.capture.grantId, grantRevision: read.capture.grantRevision } },
      regionBitmaps: { [read.capture.selector]: bitmap }, regionDigests: { [read.capture.selector]: 'same' } });
    return data;
  };
  adapter.prepareReviewedClick = cdp.prepareReviewedClick.bind(cdp);
  adapter.executeReviewedClick = cdp.executeReviewedClick.bind(cdp);
  const observe = () => service.observe({ ...owner, pageId: 'main', regions: ['#name'], reviewedClick: true,
    compositorRegion: '#name', compositorSession: 'start' });
  const act = (obs, actionId, handle = 'hname') => service.submitAction({ ...owner, pageId: 'main', actionId, authorizedIntent: 'review',
    kind: 'click', target: { handle }, observationVersion: obs.observationVersion, requireReviewedClick: true }).then(x => x.result);
  const plain = await service.observe(owner);
  await assert.rejects(service.observe({ ...owner, visibleRectangle: rect, reviewedClick: true }), { code: 'unauthenticated' });
  await assert.rejects(service.observe({ ...owner, ...admin, visibleRectangle: rect, reviewedClick: true }), { code: 'unauthorized-target' });
  await assert.rejects(act(plain, 'unreviewed'), { code: 'stale-observation' });
  const reviewed = await observe();
  assert.equal(reviewed.reviewedRgba, evidence.rgba);
  await assert.rejects(act(reviewed, 'wrong-target', 'hemail'), /only a click on that selected region|unknown-target/);
  wireResult = { admitted: false, delivered: false, reason: 'stale-compositor-revision' };
  const refused = await act(reviewed, 'late-change');
  assert.equal(refused.outcome, 'failed'); assert.equal(refused.outcomeBasis, wireResult.reason);
  wireResult = { admitted: true, delivered: true, reason: 'delivered' };
  const accepted = await act(await observe(), 'fresh');
  assert.equal(accepted.outcome, 'completed'); assert.equal(accepted.outcomeBasis, 'reviewed-input-admitted');
  wireResult = Error('connection lost');
  assert.equal((await act(await observe(), 'uncertain')).outcome, 'unknown');
  assert.equal(adapter.executions.length, 0, 'ordinary input never substituted for a native review');

  // A reviewed submit button is admitted as a submit, never as a plain click, so
  // the form's destination is authorized. While the service waits for the
  // result it skips a read region that is gone; every other read refuses one.
  await service.grantAuthorization({ ...admin, grantId: 'pixels', ownerId: 'person', intents: ['review'], actionKinds: ['click', 'submit'],
    targetOrigins: [ORIGIN], pageIds: ['main'], readRegions: ['#send'] });
  const absentRoots = [];
  const probeElement = adapter.probeElement.bind(adapter);
  adapter.probeElement = (id, selector, pageId, readRoots, absent) => { absentRoots.push(absent); return probeElement(id, selector, pageId, readRoots); };
  const observeSend = () => service.observe({ ...owner, pageId: 'main', regions: ['#send'], reviewedClick: true,
    compositorRegion: '#send', compositorSession: 'start' });
  const send = (obs, actionId, kind, extra = {}) => service.submitAction({ ...owner, pageId: 'main', actionId, authorizedIntent: 'review',
    kind, target: { handle: 'hsend' }, observationVersion: obs.observationVersion, requireReviewedClick: true, ...extra }).then(x => x.result);
  wireResult = { admitted: true, delivered: true, reason: 'delivered' };
  await assert.rejects(send(await observeSend(), 'submit-as-click', 'click'), /only a click on that selected region/);
  const completion = { completedWhen: [{ kind: 'element-present', selector: '#send' }] };
  const submitted = await send(await observeSend(), 'submit-reviewed', 'submit',
    { effect: { external: true, effectKey: 'submit-reviewed' }, completion });
  assert.equal(submitted.outcome, 'completed', JSON.stringify(submitted).slice(0, 300));
  assert.equal(submitted.outcomeBasis, 'completion-condition');
  assert.equal(adapter.executions.length, 0, 'the reviewed submit went through the native click, not an ordinary submit');
  assert.deepEqual(absentRoots, ['refuse', 'skip'], 'the check before dispatch refuses a vanished region; the read after the effect skips it');

  // What the application did is read where the finished review allows, and only what is new counts. The page
  // here is scripted: a message area that may hold old or new messages, and a "done" label.
  const human = { workspaceId: 'work', ...await service.assignOwner({ ...admin, ownerId: 'human' }) };
  const grantHuman = regions => service.grantAuthorization({ ...admin, grantId: 'human-main', ownerId: 'human', intents: ['human'],
    actionKinds: ['click', 'submit'], targetOrigins: [ORIGIN], pageIds: ['main'], readRegions: regions });
  let shown, reads;
  adapter.probeElement = async (id, selector, pageId, readRoots, absent) => {
    // The check of the target before dispatch stays with the fake page; result reads are the scripted ones.
    if (absent !== 'skip') return probeElement(id, selector, pageId, readRoots);
    reads.push({ selector, readRoots, absent });
    clock.advance(1000); // each read takes a second of the completion window
    const count = readRoots === null || readRoots.includes(selector) ? shown[selector] ?? 0 : 0;
    return { count, digest: null, text: null, value: null, choice: null, documentId: shown.document };
  };
  const look = (selector, slot, resultRegions) => service.observe({ ...human, ...admin, pageId: 'main', regions: [selector],
    reviewedClick: true, compositorRegion: selector, compositorSession: 'start', reviewSlot: slot, resultRegions });
  const review = async (selector, slot, resultRegions) => { await grantHuman([selector]); return look(selector, slot, resultRegions); };
  const finish = (obs, actionId, handle, completion) => service.submitAction({ ...human, pageId: 'main', actionId, authorizedIntent: 'human',
    kind: handle === 'hsend' ? 'submit' : 'click', target: { handle }, observationVersion: obs.observationVersion, requireReviewedClick: true,
    observationScope: { kind: 'regions', selectors: [handle === 'hsend' ? '#send' : '#name'] },
    effect: { external: true, effectKey: actionId }, completion }).then(x => x.result);
  const merge = { completedWhen: [{ kind: 'element-present', selector: '#done' }], failedWhen: [{ kind: 'element-present', selector: '#message' }] };
  const run = async (name, before, click) => {
    shown = { '#send': 1, document: 'first', ...before }; reads = []; onClick = () => Object.assign(shown, click);
    return finish(await review('#send', name, ['#message', '#done']), name, 'hsend', merge);
  };
  // Result regions are approved with the review by the page's owner, and only for a reviewed click.
  await grantHuman(['#send']);
  await assert.rejects(service.observe({ ...human, pageId: 'main', regions: ['#send'], reviewedClick: true, compositorRegion: '#send',
    compositorSession: 'start', resultRegions: ['#message'] }), { code: 'unauthenticated' });
  await assert.rejects(service.observe({ ...human, ...admin, pageId: 'main', regions: ['#send'], resultRegions: ['#message'] }), /belong to a reviewed click/);
  await assert.rejects(service.observe({ ...human, ...admin, pageId: 'main', regions: ['#send'], reviewedClick: true, compositorRegion: '#send',
    compositorSession: 'start', resultRegions: ['#a', '#b', '#c', '#d', '#e'] }), /at most 4/);

  // The application refuses without reloading: it adds a message to the same document.
  const refused400 = await run('no-reload-refusal', {}, { '#message': 1 });
  assert.equal(refused400.outcome, 'failed', JSON.stringify(refused400).slice(0, 3000));
  assert.equal(refused400.outcomeBasis, 'failure-condition');
  assert.ok(reads.length > 0 && reads.every(read => read.absent === 'skip'
    && JSON.stringify(read.readRoots) === JSON.stringify(['#send', '#message', '#done'])), JSON.stringify(reads));
  // It reloads into a refusal, or into the finished state.
  assert.equal((await run('reload-refusal', {}, { document: 'second', '#message': 1 })).outcome, 'failed');
  const merged = await run('reload-merged', {}, { document: 'second', '#done': 1 });
  assert.equal(merged.outcome, 'completed'); assert.equal(merged.outcomeBasis, 'completion-condition');

  // An old refusal is still on screen and this click changes nothing: that message is not this click's result.
  const stale = await run('old-message-only', { '#message': 1 }, {});
  assert.equal(stale.outcome, 'unknown', JSON.stringify(stale).slice(0, 300));
  assert.equal(stale.outcomeBasis, 'completion-unestablished');
  // With the old message still there, a second message or a reload is new and does count.
  assert.equal((await run('old-and-new-message', { '#message': 1 }, { '#message': 2 })).outcome, 'failed');
  assert.equal((await run('old-message-then-reload', { '#message': 1 }, { document: 'second', '#message': 1 })).outcome, 'failed');
  // The same holds for success: a "done" label left by an earlier action does not complete this one.
  assert.equal((await run('old-done-only', { '#done': 1 }, {})).outcome, 'unknown');
  assert.equal((await run('old-done-then-merged', { '#done': 1 }, { document: 'second', '#done': 1 })).outcome, 'completed');

  // Two reviews on one page under one grant. Finishing A reads A's result regions only, although B was
  // reviewed later and B's result is on the page.
  shown = { '#name': 1, '#send': 1, '#b-result': 1, document: 'first' }; reads = []; onClick = () => {};
  await grantHuman(['#name', '#send']);
  const reviewA = await look('#name', 'item-a', ['#a-result']);
  const reviewB = await look('#send', 'item-b', ['#b-result']);
  reads = [];
  const a = await finish(reviewA, 'finish-a', 'hname', { completedWhen: [{ kind: 'element-present', selector: '#b-result' }] });
  assert.equal(a.outcome, 'unknown', 'A cannot read B\'s result: ' + JSON.stringify(a).slice(0, 3000));
  assert.ok(reads.length > 0 && reads.every(read => JSON.stringify(read.readRoots) === JSON.stringify(['#name', '#a-result'])), JSON.stringify(reads.slice(0, 3)));
  reads = []; onClick = () => { shown['#b-result'] = 2; };
  const b = await finish(reviewB, 'finish-b', 'hsend', { completedWhen: [{ kind: 'element-present', selector: '#b-result' }] });
  assert.equal(b.outcome, 'completed', JSON.stringify(b).slice(0, 300));
  assert.ok(reads.every(read => JSON.stringify(read.readRoots) === JSON.stringify(['#send', '#b-result'])));
} finally { await service.close(); rmSync(root, { recursive: true, force: true }); }
console.log('ok: reviewed pixels, session binding, native-only routing, stale refusal, lost acknowledgment, reviewed form submits and review-bound results');
