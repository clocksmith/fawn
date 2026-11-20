// A real browser chooser is required. The opt-in fixture runner may preselect
// only this disposable tab on a private display. Production sets no such flag.
import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { WorkspaceController } from '../../src/client/controller.js';
import { summarizeResidentOutcome } from '../../qualification/summaries.js';
import { testBrowserSelection } from '../support/browser-selection.js';
import { WorkspaceService } from '../../src/service.js';
import { createPlaywrightAdapter } from '../../src/adapters/playwright-adapter.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';

if (process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1') {
  console.log('skip: controlled display and permission chooser required for compositor capture');
  process.exit(0);
}
const directory = mkdtempSync(join(tmpdir(), 'blitzen-compositor-'));
const server = createServer((_rq, res) => {
  res.setHeader('content-type', 'text/html');
  res.end(`<!doctype html><title>Blitzen ephemeral compositor fixture</title>
  <style>body{margin:0}canvas{isolation:isolate;position:absolute;inset:0;z-index:1}
  .tint canvas{filter:invert(1)} aside{position:absolute;inset:0 auto auto 0;width:64px;height:64px;z-index:2;pointer-events:none;background:blue}
  button{position:fixed;top:100px;z-index:3}</style>
  <canvas id="surface" width="128" height="64"></canvas><aside>private sibling</aside>
  <button id="ack" onclick="this.textContent='done'">ack</button>
  <script>const ctx = document.querySelector('canvas').getContext('2d');ctx.fillStyle='red';ctx.fillRect(0,0,128,64)</script>`);
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = 'http://127.0.0.1:' + server.address().port;
try {
  for (const [name, createAdapter] of Object.entries({ playwright: createPlaywrightAdapter, cdp: createCdpAdapter })) {
    const root = join(directory, name), base = testBrowserSelection(root).config;
    const transport = process.env.BLITZEN_TEST_COMPOSITOR_TRANSPORT ?? 'external-texture';
    const config = { ...base, observation: { ...base.observation, compositorCapture: true, compositorTransport: transport },
      browser: { ...base.browser, headless: false } };
    let adapter, lastMetric;
    const captures = [];
    const service = await WorkspaceService.open({ stateDir: root, config, createAdapter: opts => {
      adapter = createAdapter(opts);
      const snapshot = adapter.snapshot.bind(adapter);
      adapter.snapshot = async (...args) => {
        const result = await snapshot(...args);
        if (result.frameEvidence?.['#surface']) {
          lastMetric = result.regionBitmapMetrics['#surface'];
          captures.push(lastMetric);
        }
        return result;
      };
      return adapter;
    } });
    try {
      const workspaceId = 'surface';
      const admin = { workspaceId, ...await service.createWorkspace({ workspaceId, scopedReads: true }) };
      const owner = { workspaceId, ...await service.assignOwner({ ...admin, ownerId: 'observer' }) };
      const grant = { ...admin, ownerId: 'observer', pageIds: ['main'], intents: ['inspect'], actionKinds: ['navigate','click'], targetOrigins: [origin] };
      await service.grantAuthorization({ ...grant, grantId: 'navigate', readRegions: null });
      const initial = await service.observe(owner);
      const navigation = await service.submitAction({ ...owner, actionId: 'open', authorizedIntent: 'inspect', observationVersion: initial.observationVersion,
        kind: 'navigate', target: { url: origin } });
      assert.equal((await navigation.result).outcome, 'completed');
      const observe = sinceVersion => service.observe({ ...owner, regions: ['#surface'], compositorRegion: '#surface', sinceVersion });
      await assert.rejects(observe(), { code: 'unauthorized-target' }, 'a broad page grant cannot initiate a restricted surface capture');
      await service.revokeAuthorization({ ...admin, grantId: 'navigate' });
      await service.grantAuthorization({ ...grant, grantId: 'selected', readRegions: ['#surface','#ack'] });
      const evaluate = fn => name === 'playwright' ? adapter._page(workspaceId).evaluate(fn) : adapter._evaluate(workspaceId, '('+fn.toString()+')()');
      const original = await observe();
      assert.equal(original.frameEvidence['#surface'].transport, transport);
      const { width, height } = original.frameEvidence['#surface'];
      assert.ok(width > 0 && width <= 256 && width === height * 2);
      assert.equal(lastMetric.frameHostCopyBytes, transport === 'rgba-upload' ? width * height * 4 : 0);
      assert.ok(original.frameEvidence['#surface'].loaderId);
      assert.ok(Number.isSafeInteger(original.frameEvidence['#surface'].mediaTimestampUs));
      // The sibling occludes half the chosen canvas on the compositor. Its
      // color is outside the grant and must not enter the captured pixels.
      await evaluate(() => { document.querySelector('aside').style.background = 'lime'; });
      const same = await observe(original.observationVersion);
      assert.deepEqual(same.visualChanges['#surface'].changed, []);
      assert.equal(lastMetric.comparedOnGpu, true);
      assert.equal(lastMetric.tilePixelReadbackBytes, 0);
      const unchangedComparison = { ...lastMetric };
      assert.equal(unchangedComparison.gpuMaskReadbackBytes, 4);
      const backing = await service.observe({ ...owner, regions: ['#surface'] });
      await evaluate(() => { document.body.classList.add('tint'); });
      const tinted = await observe(original.observationVersion);
      assert.equal(tinted.visualChanges['#surface'].changed.length, Math.ceil(width / 64) * Math.ceil(height / 64), 'ancestor CSS changes every rendered tile');
      const verifyBacking = await service.observe({ ...owner, regions: ['#surface'] });
      assert.equal(verifyBacking.regions['#surface'], backing.regions['#surface'], 'the application backing store stayed red');
      const submit = (obs, actionId) => service.submitAction({ ...owner, actionId, kind: 'click', authorizedIntent: 'inspect',
        observationVersion: obs.observationVersion, target: { handle: obs.elements.find(x=>x.selector==='#ack').handle },
        observationScope: { kind: 'regions', selectors: ['#surface'] } });
      const timely = await observe();
      await evaluate(() => { document.body.classList.remove('tint'); });
      const stale = await submit(timely, 'stale-tint');
      assert.equal((await stale.result).neverDispatched?.code, 'stale-observation');
      const beforeReplacement = await observe();
      await evaluate(() => {
        const old = document.querySelector('canvas'), copy = old.cloneNode();
        copy.getContext('2d').drawImage(old, 0, 0); old.replaceWith(copy);
      });
      const replacement = await submit(beforeReplacement, 'different-surface');
      assert.equal((await replacement.result).neverDispatched?.code, 'stale-observation');
      const beforeRegrant = await observe();
      await service.revokeAuthorization({ ...admin, grantId: 'selected' });
      await service.grantAuthorization({ ...grant, grantId: 'selected', readRegions: ['#surface','#ack'] });
      assert.equal((await (await submit(beforeRegrant, 'reused-grant-id')).result).neverDispatched?.code, 'stale-observation');
      const wholePage = await observe();
      await evaluate(() => { document.querySelector('#ack').title = 'changed target description'; });
      const wholeAction = await service.submitAction({ ...owner, actionId: 'changed-page', kind: 'click',
        authorizedIntent: 'inspect', observationVersion: wholePage.observationVersion,
        target: { handle: wholePage.elements.find(x => x.selector === '#ack').handle } });
      assert.equal((await wholeAction.result).neverDispatched?.code, 'stale-observation');
      const firstCapture = captures.length, started = performance.now();
      const fresh = await observe();
      const ack = await submit(fresh, 'ack');
      const outcome = await ack.result;
      assert.equal(outcome.outcome, 'completed', JSON.stringify(outcome));
      assert.equal(await evaluate(() => document.querySelector('#ack').textContent), 'done');
      assert.equal(lastMetric.tilePixelReadbackBytes, width * height * 4, 'dispatch independently read every rendered pixel');
      assert.equal(await evaluate(() => typeof globalThis.__blitzenSurface), 'undefined', 'the page cannot access the private capture handle');
      const receipt = {
        clock: 'fresh-permitted-observation-through-confirmed-click-effect', transport, adapter: name,
        selection: config.browser.channel === 'fawn' ? config.browser.fawnBuild : { channel: config.browser.channel },
        dimensions: [width, height], unchangedComparison,
        ...summarizeResidentOutcome({ elapsedMs: performance.now() - started, verified: true,
          ruleEvaluations: 1, modelCalls: [], captures: captures.slice(firstCapture) }),
      };
      if (process.env.BLITZEN_COMPOSITOR_RECEIPT_PREFIX) writeFileSync(
        process.env.BLITZEN_COMPOSITOR_RECEIPT_PREFIX + '-' + name + '.json',
        JSON.stringify(receipt, null, 2) + '\n', { flag: 'wx' });
      const controller = new WorkspaceController({ service, stateDir: join(root, 'client'),
        config: JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url), 'utf8')) });
      try {
        const project = await controller.create({ goal: 'Keep selected visual evidence with the investigation' });
        await controller.navigate(project.id, { pageId: 'main', url: origin });
        const first = await controller.capture(project.id, { pageId: 'main', compositorRegion: '#surface' });
        assert.equal(first.source.compositor.selector, '#surface');
        const refresh = () => controller.capture(project.id, { pageId: 'main', artifactId: first.id, expectedRevision: first.revision });
        assert.equal((await refresh()).revision, first.revision, 'the same granted pixels preserve the evidence revision');
        const tint = () => document.body.classList.add('tint');
        if (name === 'playwright') await adapter._page(project.id).evaluate(tint);
        else await adapter._evaluate(project.id, '('+tint.toString()+')()');
        const next = await refresh();
        assert.equal(next.revision, first.revision + 1);
        const continuity = ['width', 'height', 'transport', 'loaderId', 'surfaceId', 'grantRevision']
          .every(key => next.source.compositor[key] === first.source.compositor[key]);
        assert.equal(next.source.visualChanges.baseline, continuity ? 'compared' : 'new',
          JSON.stringify({ earlier: first.source.compositor, later: next.source.compositor }));
        assert.ok(next.source.visualChanges.changed.length);
        assert.notEqual(next.source.sha256, first.source.sha256, 'an actual compositor-only change revises saved evidence');
      } finally { await controller.close(); }
      console.log('ok:', name, transport, width + 'x' + height, 'browser-restricted compositor frame, sibling excluded, CSS diffed, stale action refused and a fresh action completed');
    } finally { await service.close(); }
  }
} finally {
  await new Promise(resolve => server.close(resolve));
  rmSync(directory, { recursive: true, force: true });
}
