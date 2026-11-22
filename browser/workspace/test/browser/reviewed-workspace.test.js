import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { openDrawingSite } from '../../qualification/reviewed-drawing-site.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { loadClientServiceConfig } from '../../src/client/browser-selection.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { startWorkspaceServer } from '../../src/client/server.js';
import { WorkspaceService } from '../../src/service.js';

const profile = process.env.FAWN_REVIEWED_INPUT_PROFILE;
if (!profile) {
  console.log('skip: requires the native reviewed-input browser and a controlled capture chooser');
  process.exit(0);
}
const root = mkdtempSync(join(tmpdir(), 'reviewed-workspace-'));
const clientConfig = JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url)));
const browserConfig = loadClientServiceConfig({ browserChannel: 'fawn', browserConfig: profile,
  headless: false, compositorTransport: 'stock-i420' });
const site = await openDrawingSite();
let server, controller, service, adapter, repaint = false;
try {
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config: browserConfig, createAdapter(opts) {
    adapter = createCdpAdapter(opts);
    const dispatch = adapter.executeReviewedClick.bind(adapter);
    adapter.executeReviewedClick = async (id, pageId, review) => {
      if (repaint) {
        repaint = false;
        await adapter._evaluate({ workspaceId: id, pageId }, `(async()=>{
          const ctx=document.querySelector('canvas').getContext('2d');ctx.fillStyle='blue';ctx.fillRect(0,0,1,1);
          await new Promise(r=>requestAnimationFrame(()=>requestAnimationFrame(r)));return true;
        })()`);
      }
      return dispatch(id, pageId, review);
    };
    return adapter;
  } });
  controller = new WorkspaceController({ service, stateDir: join(root, 'client'), config: clientConfig });
  server = await startWorkspaceServer({ controller, config: clientConfig, port: 0 });
  let cookie, projectId;
  async function post(path, body) {
    const response = await fetch(server.origin + path, { method: 'POST', headers: { origin: server.origin,
      'content-type': 'application/json', ...(cookie ? { cookie } : {}) }, body: JSON.stringify(body) });
    cookie ??= response.headers.get('set-cookie')?.split(';')[0];
    const data = await response.json();
    if (!response.ok) throw Object.assign(Error(data.error), { code: data.code, details: data.details });
    return data;
  }
  const command = (operation, input) => post('/api/command', { operation, projectId, input });
  await post('/api/session', { token: new URL(server.loginUrl).hash.slice(1) });
  ({ id: projectId } = await command('create', { goal: 'Review a disposable repair drawing' }));
  await command('navigate', { pageId: 'main', url: site.login('recipient') });
  await command('focusPage', { pageId: 'main' });
  // The test owner authorizes this exact viewport rectangle, including any painted
  // overlays. Production users get these coordinates and document identity from selectRegion.
  const { cdp, sessionId } = adapter._entry({ workspaceId: projectId, pageId: 'main' });
  const { frameTree: { frame } } = await cdp.send('Page.getFrameTree', {}, sessionId);
  const capture = () => command('reviewClickTarget', { pageId: 'main', selector: '#drawing',
    visibleRectangle: { url: frame.url, loaderId: frame.loaderId, selector: '#drawing', x: 0, y: 0, width: 48, height: 48 } });
  const act = (image, actionId) => command('clickReviewedTarget', { pageId: 'main', handle: image.handle,
    observationVersion: image.observationVersion, actionId,
    effect: { external: true, effectKey: 'review-' + actionId }, completion: {
      completedWhen: [{ kind: 'element-present', selector: '#drawing #write-receipt' }],
    } });
  const original = await capture();
  assert.equal(original.pixelScope, 'visible-rectangle');
  assert.equal(createHash('sha256').update(Buffer.from(original.rgba, 'base64')).digest('hex'), original.baselineDigest);
  repaint = true;
  const stale = await act(original, 'stale');
  assert.equal(stale.outcome, 'failed', JSON.stringify(stale));
  assert.equal(stale.outcomeBasis, 'stale-compositor-revision');
  assert.equal(site.attempts.length, 0);
  const renewed = await capture();
  assert.notEqual(renewed.baselineDigest, original.baselineDigest);
  const fresh = await act(renewed, 'fresh');
  assert.equal(fresh.outcome, 'completed', JSON.stringify(fresh));
  assert.equal(site.receipts.size, 1);
  // A refused capture interrupts the reviewer but leaves no approval behind:
  // the earlier review cannot be spent, and one fresh review restores progress.
  // Device emulation is one condition the browser refuses to capture under.
  // A fresh document, so its completion check starts unmet.
  await command('navigate', { pageId: 'main', url: site.login('recipient') });
  const { frameTree: { frame: next } } = await cdp.send('Page.getFrameTree', {}, sessionId);
  Object.assign(frame, { url: next.url, loaderId: next.loaderId });
  const earlier = await capture();
  const attemptsBeforeRefusal = site.attempts.length;
  const { innerWidth: width, innerHeight: height } = await adapter._evaluate({ workspaceId: projectId, pageId: 'main' },
    '({ innerWidth, innerHeight })');
  await cdp.send('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }, sessionId);
  const refused = await capture().then(() => null, error => error);
  await cdp.send('Emulation.clearDeviceMetricsOverride', {}, sessionId);
  assert(refused, 'capture under device emulation must be refused');
  assert.equal(refused.code, 'review-capture-refused', refused.message);
  assert.equal(refused.details.retryable, true);
  assert.equal(refused.details.priorApprovalCleared, true);
  const spent = await act(earlier, 'after-refusal');
  assert.notEqual(spent.outcome, 'completed', JSON.stringify(spent));
  assert.equal(site.attempts.length, attemptsBeforeRefusal);
  assert.equal(spent.outcomeBasis, 'review-expired', JSON.stringify(spent));
  // The fresh review is delivered and reaches the application, which applies
  // its own record-version check to the second write.
  const recovered = await act(await capture(), 'recovered');
  assert.equal(recovered.evidence?.effect?.delivered, true, JSON.stringify(recovered));
  assert.equal(site.attempts.length, attemptsBeforeRefusal + 1);
  console.log(JSON.stringify({ authenticatedWorkspaceApi: true, staleInput: stale.outcomeBasis,
    staleSiteAttempts: 0, freshWrites: 1, account: site.attempts[0].account,
    captureRefusal: { code: refused.code, reason: refused.details.reason, earlierApproval: spent.outcomeBasis ?? spent.outcome,
      siteAttemptsFromEarlierApproval: 0, freshReviewDelivered: true } }));
} finally {
  try { await server?.close(); await controller?.close(); await service?.close(); }
  finally { await site.close(); rmSync(root, { recursive: true, force: true }); }
}
