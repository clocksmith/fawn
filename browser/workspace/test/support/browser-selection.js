// Tests name their stock control explicitly. A selected profile exercises the same
// product admission boundary without altering the frozen service configuration.
import { loadClientServiceConfig } from '../../src/client/browser-selection.js';

export function testBrowserSelection(directory) {
  const browserConfig = process.env.FAWN_WORKSPACE_TEST_BROWSER_CONFIG;
  const selection = browserConfig ? { browserConfig, browserChannel: 'fawn' }
    : { browserChannel: process.env.FAWN_WORKSPACE_TEST_BROWSER_CHANNEL ?? 'chrome' };
  return { selection, config: loadClientServiceConfig({ directory, ...selection, headless: true }) };
}
