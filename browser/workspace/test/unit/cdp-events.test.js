import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { chmodSync, mkdtempSync, readFileSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { loadServiceConfig } from '../../src/config.js';
import { createCdpAdapter } from '../../src/adapters/cdp-adapter.js';

if (process.platform === 'win32') {
  console.log('skip: actual POSIX pipe peer; Windows browser transport requires its platform control');
} else {
  const root = realpathSync.native(mkdtempSync(join(tmpdir(), 'fawn-cdp-events-')));
  const source = readFileSync(new URL('../support/cdp-controlled-browser.cjs', import.meta.url), 'utf8');
  const unhandled = [];
  const onUnhandled = error => unhandled.push(String(error));
  process.on('unhandledRejection', onUnhandled);
  try {
    for (const scenario of ['command-rejection', 'navigation-error', 'event-timeout', 'early-event',
      'coalesced-download', 'click-rejection', 'click-timeout', 'transport-close']) {
      const record = join(root, scenario + '.json');
      const executable = join(root, scenario + ' controlled browser');
      const timeoutMs = 80;
      const script = `#!${process.execPath}\nglobal.CONTROL=${JSON.stringify({ scenario, record, timeoutMs })};\n${source}`;
      writeFileSync(executable, script);
      chmodSync(executable, 0o700);
      const config = structuredClone(loadServiceConfig());
      config.dispatch.actionTimeoutMs = timeoutMs;
      config.browser.channel = 'fawn';
      config.browser.fawnBuild = { executable: { path: executable,
        sha256: createHash('sha256').update(script).digest('hex') }, dependencies: [], runtime: { mode: 'dawn' } };
      const adapter = createCdpAdapter({ config, profileRoot: join(root, scenario, 'profiles'), artifactRoot: join(root, scenario, 'artifacts') });
      try {
        await adapter.openWorkspace('controlled');
        if (scenario === 'coalesced-download') {
          const downloaded = await adapter.execute('controlled', { kind: 'download', target: { selector: '#download' }, actionId: 'save' });
          assert.equal(readFileSync(downloaded.artifactPath, 'utf8'), 'download proof');
          assert.equal(downloaded.suggestedFilename, 'proof.txt');
        } else if (scenario.startsWith('click-')) {
          await assert.rejects(adapter.execute('controlled', { kind: 'download', target: { selector: '#download' }, actionId: 'save' }),
            scenario === 'click-rejection' ? /controlled command rejection/ : /not received|timed out/);
        } else if (scenario === 'early-event') {
          assert.equal((await adapter.execute('controlled', { kind: 'navigate', target: { url: 'http://controlled/' } })).finalUrl, 'http://controlled/');
        } else {
          await assert.rejects(adapter.execute('controlled', { kind: 'navigate', target: { url: 'http://controlled/' } }),
            scenario === 'transport-close' ? /pipe closed|Chromium ended/ : /controlled.*rejection|not received|timed out/);
        }
        await new Promise(resolve => setTimeout(resolve, timeoutMs * 3));
        assert.deepEqual(unhandled, [], `${scenario}: event/command rejection must remain owned`);
        const observed = JSON.parse(readFileSync(record));
        if (scenario === 'click-timeout') {
          assert.ok(!observed.commands.some(command => command.startsWith('Input.dispatchMouseEvent')),
            'late point response must not dispatch mouse events after group timeout');
        }
      } finally {
        await adapter.close();
        const { pid } = JSON.parse(readFileSync(record));
        assert.throws(() => process.kill(pid, 0), { code: 'ESRCH' }, `${scenario}: owned peer must be reaped`);
      }
    }
    console.log('ok: grouped CDP waits, early/coalesced events, cancellation, pipe close and no unhandled rejections');
  } finally {
    process.removeListener('unhandledRejection', onUnhandled);
    rmSync(root, { recursive: true, force: true });
  }
}
