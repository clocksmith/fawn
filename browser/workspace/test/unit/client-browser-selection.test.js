import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtempSync, realpathSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { loadServiceConfig } from '../../src/config.js';
import { loadClientServiceConfig } from '../../src/client/browser-selection.js';

const directory = realpathSync.native(mkdtempSync(join(tmpdir(), 'fawn-client-browser-')));
const path = join(directory, 'browser-profile.json');
const artifact = name => {
  const path = join(directory, name);
  writeFileSync(path, name);
  return { path, sha256: createHash('sha256').update(name).digest('hex') };
};
try {
  assert.throws(() => loadClientServiceConfig({ directory }), /No stock-browser fallback/);
  for (const browserChannel of ['chrome', 'chromium', 'chromium-headless-shell']) {
    assert.equal(loadClientServiceConfig({ directory, browserChannel }).browser.channel, browserChannel);
  }
  assert.throws(() => loadClientServiceConfig({ directory, browserChannel: 'unknown' }));
  const profile = structuredClone(loadServiceConfig());
  profile.browser = { ...profile.browser, channel: 'fawn', fawnBuild: {
    executable: artifact('fawn-image'), dependencies: [artifact('compiler')],
    runtime: { mode: 'doe-c', library: artifact('doe-library') },
  } };
  const save = value => writeFileSync(path, JSON.stringify(value));
  save(profile);
  assert.deepEqual(loadClientServiceConfig({ directory }), profile);
  assert.deepEqual(loadClientServiceConfig({ directory, browserChannel: 'fawn' }), profile);
  assert.equal(loadClientServiceConfig({ directory, headless: false }).browser.headless, false);
  assert.deepEqual(loadClientServiceConfig({ directory: '/unused', browserConfig: path }), profile);
  assert.throws(() => loadClientServiceConfig({ directory, browserConfig: path, browserChannel: 'chrome' }), /not both/);
  for (const select of [value => value.browser.fawnBuild.executable,
    value => value.browser.fawnBuild.dependencies[0], value => value.browser.fawnBuild.runtime.library]) {
    const changed = structuredClone(profile);
    select(changed).sha256 = '0'.repeat(64);
    save(changed);
    assert.throws(() => loadClientServiceConfig({ directory }), /SHA-256 mismatch/);
  }
  const changedPolicy = structuredClone(profile);
  changedPolicy.dispatch.actionTimeoutMs++;
  save(changedPolicy);
  assert.throws(() => loadClientServiceConfig({ directory }), /only browser configuration/);
  save(loadServiceConfig());
  assert.throws(() => loadClientServiceConfig({ directory }), /must select Fawn/);
  writeFileSync(path, '{');
  assert.throws(() => loadClientServiceConfig({ directory }), SyntaxError);
  console.log('ok: Fawn default, explicit stock controls, pinned closure, frozen service policy, no fallback');
} finally { rmSync(directory, { recursive: true, force: true }); }
