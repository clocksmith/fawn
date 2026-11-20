// A canvas supplies the only status signal. The site's revision-conditional form
// and its durable review record establish which job can be queued and recovered.
import assert from 'node:assert/strict';
import { appendFileSync, existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { WorkspaceService } from '../../src/service.js';
import { createPlaywrightAdapter } from '../../src/adapters/playwright-adapter.js';
import { testBrowserSelection } from '../support/browser-selection.js';
import { assertSolidPixels } from '../support/gpu-presentation-oracle.js';

const root = mkdtempSync(join(tmpdir(), 'blitzen-canvas-review-'));
const log = join(root, 'commits.jsonl');
const committed = () => existsSync(log) ? readFileSync(log, 'utf8').trim().split('\n').map(line => JSON.parse(line)) : [];
const attempts = [];
// The grant only covers the review controls and the canvas. Every route keeps
// those roots identifiable, including the authoritative GET after a lost reply.
const layout = controls => `<!doctype html><main id="controls">${controls}</main>
  <canvas id="cell" width="8" height="8"></canvas><aside>Outside the review grant</aside>`;
let current = { job: 'A', revision: 1 };
const key = ({ job, revision }) => `${job}-r${revision}`;
const pathFor = (row) => `/queue/${key(row)}`;
const server = createServer((req, res) => {
  res.setHeader('Cache-Control', 'no-store');
  if (req.method === 'POST') {
    attempts.push(req.url);
    if (req.url !== pathFor(current)) {
      res.writeHead(409, { 'content-type': 'text/html' });
      res.end(layout('<div id="conflict">Schedule changed</div>'));
      return;
    }
    appendFileSync(log, JSON.stringify(current) + '\n');
    res.writeHead(303, { location: '/accepted' });
    res.end();
    return;
  }
  res.setHeader('content-type', 'text/html');
  if (req.url === '/records') {
    res.end(layout(committed().map(row => `<div id="review-${key(row)}">Queued</div>`).join('')));
  } else if (req.url === '/accepted') {
    res.end(layout('Accepted'));
  } else {
    res.end(layout(`<form method="post" action="${pathFor(current)}"><button id="queue">Queue review</button></form>`)
      + "<script>const ctx = document.querySelector('canvas').getContext('2d'); ctx.fillStyle='red'; ctx.fillRect(0,0,8,8)</script>");
  }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let service, adapter;
try {
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config: testBrowserSelection(root).config,
    createAdapter: options => {
      adapter = createPlaywrightAdapter(options);
      const execute = adapter.execute.bind(adapter);
      adapter.execute = async (id, request, files) => {
        // A new revision lands after Blitzen's final check, before site commit.
        if (request.actionId === 'revision-race') current = { ...current, revision: current.revision + 1 };
        const result = await execute(id, request, files);
        if (request.actionId === 'lost-ack') throw Error('Test discarded the acknowledgment after commit');
        return result;
      };
      return adapter;
    } });
  const workspaceId = 'review';
  const admin = { workspaceId, ...await service.createWorkspace({ workspaceId, scopedReads: true }) };
  const owner = { workspaceId, ...await service.assignOwner({ ...admin, ownerId: 'reviewer' }) };
  const grant = { ...admin, ownerId: 'reviewer', pageIds: ['main'], targetOrigins: [origin], intents: ['queue-review'] };
  await service.grantAuthorization({ ...grant, grantId: 'open-local-site', readRegions: null, actionKinds: ['navigate'] });
  const observe = () => service.observe({ ...owner, regions: ['#cell'] });
  const submit = async (observation, spec) => (await service.submitAction({
    ...owner, authorizedIntent: 'queue-review', observationVersion: observation.observationVersion, ...spec,
  })).result;
  const navigate = async (id, path) => {
    const result = await submit(await service.observe(owner), { actionId: id, kind: 'navigate', target: { url: origin + path } });
    assert.equal(result.outcome, 'completed');
  };
  // This reviewed application encodes both identifiers in an immutable form
  // destination. A visually identical cell without that contract cannot act.
  const proposal = (observation, actionId) => {
    const target = observation.elements.find(element => element.selector === '#queue');
    const route = target?.formAction && new URL(target.formAction);
    const id = route?.origin === origin && /^\/queue\/([A-Z]-r\d+)$/.exec(route.pathname)?.[1];
    if (!id) return null;
    return { actionId, kind: 'submit', target: { handle: target.handle },
      observationScope: { kind: 'regions', selectors: ['#cell'] },
      effect: { external: true, effectKey: 'review-' + id },
      completion: { completedWhen: [{ kind: 'element-present', selector: '#review-' + id }],
        failedWhen: [{ kind: 'element-present', selector: '#conflict' }] } };
  };
  await navigate('open', '/schedule');
  await service.revokeAuthorization({ ...admin, grantId: 'open-local-site' });
  await service.grantAuthorization({ ...grant, grantId: 'review-regions', readRegions: ['#controls', '#cell'],
    actionKinds: ['navigate', 'submit'] });
  await assert.rejects(service.observe({ ...owner, regions: ['aside'] }), { code: 'unauthorized-target' });
  const page = adapter._page(workspaceId); // Fixture controls and independent compositor oracle only.
  const cell = page.locator('#cell');
  const red = () => cell.screenshot().then(png => assertSolidPixels(png, [255, 0, 0, 255]));
  const original = await observe();
  const originalImage = await red();
  assert.ok(original.regions['#cell']);
  assert.ok(!original.text.includes('Outside the review grant'));
  await cell.evaluate(el => { const ctx = el.getContext('2d'); ctx.fillStyle = 'blue'; ctx.fillRect(0, 0, 8, 8); });
  const pixelRefusal = await submit(original, proposal(original, 'repainted'));
  assert.equal(pixelRefusal.neverDispatched?.code, 'stale-observation');
  const blue = await observe();
  assert.equal(blue.contentHash, original.contentHash, 'canvas-only repaint preserves markup and control values');
  assert.notEqual(blue.regions['#cell'], original.regions['#cell']);
  assertSolidPixels(await cell.screenshot(), [0, 0, 255, 255]);

  await cell.evaluate(el => { el.width = 0; });
  assert.equal((await observe()).regions['#cell'], null, 'an empty bitmap cannot bind a region');
  await page.reload();
  const beforeSwap = await observe();
  current = { job: 'B', revision: 1 };
  await page.reload();
  assert.deepEqual(await red(), originalImage);
  const stale = await submit(beforeSwap, { ...proposal(beforeSwap, 'lookalike'), resubmissionOf: 'repainted' });
  assert.equal(stale.neverDispatched?.code, 'target-changed');
  await page.locator('form').evaluate(form => { form.action = '/queue'; });
  assert.equal(proposal(await observe(), 'unbound'), null);
  assert.deepEqual(attempts, []);

  await page.reload();
  const beforeRace = await observe();
  const raced = await submit(beforeRace, proposal(beforeRace, 'revision-race'));
  assert.equal(raced.outcome, 'failed');
  assert.deepEqual(attempts, ['/queue/B-r1']);
  assert.deepEqual(committed(), []);

  await navigate('reopen', '/schedule');
  const valid = await observe();
  const accepted = proposal(valid, 'lost-ack');
  const lost = await submit(valid, accepted);
  assert.equal(lost.isUncertain, true);
  assert.deepEqual(committed(), [{ job: 'B', revision: 2 }]);
  const reconcile = () => service.reconcile({ ...owner, actionId: 'lost-ack', method: 'application-state',
    effectPresentWhen: [{ kind: 'url-equals', value: origin + '/records' }, ...accepted.completion.completedWhen] });
  assert.equal((await reconcile()).reconciliation.result, 'inconclusive');
  await navigate('lookup', '/records');
  assert.equal((await reconcile()).reconciliation.result, 'effect-present');
  await navigate('return', '/schedule');
  const after = await observe();
  await assert.rejects(submit(after, proposal(after, 'duplicate')), { code: 'effect-already-completed' });
  assert.deepEqual(attempts, ['/queue/B-r1', '/queue/B-r2']);
  assert.deepEqual(committed(), [{ job: 'B', revision: 2 }]);
  console.log('ok: canvas-only repaint and identical-pixel job swap refused before POST, revision race rejected at site, one committed review recovered without repeat POST');
} finally {
  await service?.close();
  await new Promise(resolve => server.close(resolve));
  rmSync(root, { recursive: true, force: true });
}
