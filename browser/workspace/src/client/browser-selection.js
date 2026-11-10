// Product launch defaults to an explicitly pinned Fawn build; stock browsers are opt-in controls.
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { isDeepStrictEqual } from 'node:util';
import { loadServiceConfig, validateServiceConfig } from '../config.js';
import { prepareFawnLaunch } from '../browser-launch.js';

export function loadClientServiceConfig({ directory, browserConfig, browserChannel, headless }) {
  if (browserChannel && browserChannel !== 'fawn') {
    if (browserConfig) throw Error('Choose a Fawn browser profile or an explicit stock channel, not both');
    return loadServiceConfig(undefined, { browserChannel, headless });
  }
  const path = browserConfig ?? join(directory, 'browser-profile.json');
  let profile;
  try { profile = JSON.parse(readFileSync(path, 'utf8')); }
  catch (error) {
    if (error.code === 'ENOENT') throw Error(`Fawn browser profile missing: ${path}. Create it with qualification/create-browser-profile.js or select --browser-config. No stock-browser fallback.`);
    throw error;
  }
  const { browser, ...policy } = validateServiceConfig(profile);
  const { browser: _baseBrowser, ...basePolicy } = loadServiceConfig();
  if (!isDeepStrictEqual(policy, basePolicy)) throw Error('Browser profile may change only browser configuration');
  if (browser.channel !== 'fawn') throw Error('Product browser profile must select Fawn; use an explicit stock channel for comparison');
  const config = validateServiceConfig({ ...profile, browser: { ...browser,
    ...(headless === undefined ? {} : { headless }) } });
  prepareFawnLaunch(config.browser);
  return config;
}
