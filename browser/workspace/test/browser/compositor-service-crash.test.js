// The service and Chromium are real child processes. The fixture owns the
// temporary profile and checks that losing the service also ends its browser.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { setTimeout as delay } from 'node:timers/promises';
import { WorkspaceService } from '../../src/service.js';
import { createPlaywrightAdapter } from '../../src/adapters/playwright-adapter.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';
import { startHost } from '../../qualification/service-client.js';
import { testBrowserSelection } from '../support/browser-selection.js';

if (process.env.BLITZEN_TEST_ELEMENT_CAPTURE !== '1' || process.platform === 'win32') {
  console.log('skip: visible disposable browser, permission chooser and POSIX process checks required');
  process.exit(0);
}
const root = mkdtempSync(join(tmpdir(), 'blitzen-capture-crash-'));
const server = createServer((_req, res) => {
  res.setHeader('Content-Type', 'text/html');
  res.end('<title>Blitzen ephemeral compositor fixture</title><canvas style="isolation:isolate" id="scope" width="32" height="32"></canvas>');
});
await new Promise(done => server.listen(0, '127.0.0.1', done));
const origin = 'http://127.0.0.1:' + server.address().port;
const profileCommands = directory => execFileSync('ps', ['-axo','stat=,args='], { encoding: 'utf8' }).split('\n')
  .filter(line => !line.trimStart().startsWith('Z') && line.split(/\s+/).includes('--user-data-dir=' + directory));
try {
  for (const adapter of ['playwright', 'cdp']) {
    const stateDir = join(root, adapter), config = testBrowserSelection(root).config;
    const browserConfig = join(root, adapter + '.json');
    const liveConfig = { ...config, browser: { ...config.browser, headless: false },
      observation: { ...config.observation, compositorCapture: true, compositorTransport: process.env.BLITZEN_TEST_COMPOSITOR_TRANSPORT ?? 'external-texture' } };
    writeFileSync(browserConfig, JSON.stringify(liveConfig));
    const start = () => startHost({ stateDir, browserConfig, adapter, headless: false });
    const workspaceId = 'capture', profile = join(stateDir, 'profiles', workspaceId);
    let host = await start();
    try {
      const admin = { workspaceId, ...await host.call('createWorkspace', { workspaceId, scopedReads: true }) };
      const owner = { workspaceId, ...await host.call('assignOwner', { ...admin, ownerId: 'human' }) };
      const grant = { ...admin, ownerId: 'human', pageIds: ['main'], intents: ['inspect'], actionKinds: ['navigate'], targetOrigins: [origin] };
      const navigate = async id => {
        await host.call('grantAuthorization', { ...grant, grantId: 'nav', readRegions: null });
        const view = await host.call('observe', owner);
        const result = await host.submitAndWait({ ...owner, actionId: id, kind: 'navigate', target: { url: origin },
          authorizedIntent: 'inspect', observationVersion: view.observationVersion });
        assert.equal(result.outcome, 'completed');
        await host.call('revokeAuthorization', { ...admin, grantId: 'nav' });
      };
      await navigate('before');
      await host.call('grantAuthorization', { ...grant, grantId: 'scope', readRegions: ['#scope'] });
      const observe = session => host.call('observe', { ...owner, compositorRegion: '#scope', regions: ['#scope'], compositorSession: session });
      const current = await observe('start'), id = current.frameEvidence['#scope'].sessionId;
      assert.ok(profileCommands(profile).length, 'the fixture service owns a live browser process');
      assert.equal((await host.kill()).signal, 'SIGKILL');
      host = null;
      const until = Date.now() + 12000;
      while (Date.now() < until && profileCommands(profile).length) await delay(150);
      assert.deepEqual(profileCommands(profile), [], 'loss of the service pipe retired its actual browser and stream');
      let browser;
      const service = await WorkspaceService.open({ stateDir, config: liveConfig, createAdapter: options => {
        browser = (adapter === 'playwright' ? createPlaywrightAdapter : createCdpAdapter)(options);
        return browser;
      } });
      try {
        // The person reopens the same authorized origin on the fresh browser.
        // Preserve the original journaled grant and owner for this check.
        await service.inspect(admin);
        await browser.execute(workspaceId, { kind: 'navigate', target: { url: origin } });
        const again = session => service.observe({ ...owner, compositorRegion: '#scope', regions: ['#scope'], compositorSession: session });
        await assert.rejects(again(id), /expired|changed|ended/);
        const renewed = (await again('start')).frameEvidence['#scope'];
        assert.notEqual(renewed.sessionId, id);
        assert.equal(renewed.grantRevision, current.frameEvidence['#scope'].grantRevision);
      } finally { await service.close(); }
      console.log('ok:', adapter, 'service SIGKILL ended browser; restored journal refused old capture and a new explicit share succeeded');
    } finally { await host?.close(); }
  }
} finally {
  await new Promise(resolve => server.close(resolve));
  rmSync(root, { recursive: true, force: true });
}
