// Exercise the raw CDP remote object across a document replacement, a fresh
// service process state, and a narrower read grant on the same origin.
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { WorkspaceService } from '../../src/service.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { testBrowserSelection } from '../support/browser-selection.js';

const root = mkdtempSync(join(tmpdir(), 'blitzen-canvas-cdp-'));
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html');
  response.end(`<!doctype html><canvas id="selected" width="66" height="65"></canvas><aside>outside</aside>
    <script>const ctx = document.querySelector('canvas').getContext('2d');
    ctx.fillStyle = 'red'; ctx.fillRect(0, 0, 66, 65)</script>`);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let service, adapter;
try {
  const base = testBrowserSelection(root).config;
  const canvasBackend = process.env.BLITZEN_TEST_CANVAS_BACKEND ?? 'cpu';
  const open = () => WorkspaceService.open({ stateDir: join(root, 'service'),
    config: { ...base, observation: { ...base.observation, canvasBackend } },
    createAdapter(options) { adapter = createCdpAdapter(options); return adapter; } });
  service = await open();
  const workspaceId = 'cdp';
  const admin = { workspaceId, ...await service.createWorkspace({ workspaceId, scopedReads: true }) };
  const owner = { workspaceId, ...await service.assignOwner({ ...admin, ownerId: 'reader' }) };
  const grant = { ...admin, ownerId: 'reader', pageIds: ['main'], targetOrigins: [origin], intents: ['review'], actionKinds: ['navigate'] };
  await service.grantAuthorization({ ...grant, grantId: 'all', readRegions: null });
  async function navigate(actionId, path) {
    const frame = await service.observe(owner);
    const { result } = await service.submitAction({ ...owner, observationVersion: frame.observationVersion,
      actionId, authorizedIntent: 'review', kind: 'navigate', target: { url: origin + path } });
    const receipt = await result;
    assert.equal(receipt.outcome, 'completed', JSON.stringify(receipt));
  }
  const observe = sinceVersion => service.observe({ ...owner, regions: ['#selected'], sinceVersion });
  await navigate('first-document', '/one');
  const red = await observe();
  assert.ok(red.regions['#selected']);
  await adapter._evaluate(workspaceId, "ctx.fillStyle = 'blue'; ctx.fillRect(65, 64, 1, 1)");
  const blue = await observe(red.observationVersion);
  assert.deepEqual(blue.visualChanges['#selected'].changed, [{ x: 64, y: 64, width: 2, height: 1 }]);
  await service.revokeAuthorization({ ...admin, grantId: 'all' });
  await service.grantAuthorization({ ...grant, grantId: 'selected', readRegions: ['#selected'] });
  assert.deepEqual((await observe(blue.observationVersion)).visualChanges['#selected'].changed, []);
  await assert.rejects(service.observe({ ...owner, regions: ['aside'] }), { code: 'unauthorized-target' });
  await navigate('another-document', '/two');
  const reset = await observe(blue.observationVersion);
  assert.equal(reset.visualChanges['#selected'].baseline, 'new', 'a different URL cannot supply a delta base');
  await service.grantAuthorization({ ...grant, grantId: 'return', readRegions: null });
  await service.revokeAuthorization({ ...admin, grantId: 'selected' });
  await service.close();
  service = await open();
  // A browser opens on a blank main page after restart. The caller must acquire
  // it under a navigation grant before comparing against a saved URL.
  await navigate('after-restart', '/one');
  const recovered = await observe(blue.observationVersion);
  assert.deepEqual(recovered.visualChanges['#selected'], { width: 66, height: 65, baseline: 'compared',
    changed: [{ x: 64, y: 64, width: 2, height: 1 }] });
  console.log('ok: raw CDP canvas changes, scope change, URL isolation and durable tile comparison across service restart');
} finally {
  await service?.close();
  await new Promise(resolve => server.close(resolve));
  rmSync(root, { recursive: true, force: true });
}
