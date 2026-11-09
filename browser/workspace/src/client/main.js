#!/usr/bin/env node
// Run the product surface over stock Chromium; no Chromium fork or GPU claim is needed.
import { readFile } from 'node:fs/promises';
import { mkdirSync, chmodSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { homedir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { loadServiceConfig } from '../config.js';
import { WorkspaceService } from '../service.js';
import { createPlaywrightAdapter } from '../adapters/playwright-adapter.js';
import { createCdpAdapter } from '../adapters/cdp-adapter.js';
import { createCliPlanner } from './cli-planner.js';
import { createPlanner } from './planner.js';
import { WorkspaceController } from './controller.js';
import { WorkflowStore } from './workflow-store.js';
import { startWorkspaceServer } from './server.js';

export async function openWorkspaceApp({ stateDir, port, headless = false, adapter = 'playwright', config: supplied, planner: suppliedPlanner,
  workflowPersistence = null, workflowTasks = new Map() } = {}) {
  const config = supplied ?? JSON.parse(await readFile(new URL('../../../../config/fawn-workspace-client.json', import.meta.url), 'utf8'));
  for (const field of ['port', 'maxBodyBytes', 'maxEvidenceBytes', 'maxRepositoryFiles', 'maxTurns', 'maxInputBytes', 'maxOutputTokens', 'modelTimeoutMs', 'pollMs', 'delegationMaxAgeMs', 'shutdownWaitMs', 'cliTerminationGraceMs']) {
    if (!Number.isSafeInteger(config[field]) || config[field] < (field === 'port' ? 0 : 1)) throw Error(`Invalid client config ${field}`);
  }
  if (!Number.isFinite(config.cliMaxBudgetUsd) || config.cliMaxBudgetUsd <= 0) throw Error('Invalid CLI planner budget');
  if (process.env.FAWN_PLANNER && !['claude-cli', 'anthropic'].includes(process.env.FAWN_PLANNER)) throw Error('Unknown planner selection');
  if (config.host !== '127.0.0.1') throw Error('Workspace control service only listens on loopback');
  const directory = resolve(stateDir ?? process.env.FAWN_WORKSPACE_STATE_DIR ?? join(homedir(), '.local/state/fawn-workspace'));
  mkdirSync(directory, { recursive: true, mode: 0o700 }); chmodSync(directory, 0o700);
  const planner = suppliedPlanner ?? (process.env.FAWN_PLANNER === 'claude-cli' ? createCliPlanner({
    executable: process.env.FAWN_CLAUDE_BIN ?? 'claude', model: process.env.FAWN_MODEL, directory: join(directory, 'planner'), config
  }) : createPlanner({ model: process.env.FAWN_MODEL, endpoint: config.modelEndpoint,
    apiKey: process.env[config.apiKeyEnvironment], maxOutputTokens: config.maxOutputTokens, timeoutMs: config.modelTimeoutMs }));
  const factory = { playwright: createPlaywrightAdapter, cdp: createCdpAdapter }[adapter];
  if (!factory) throw Error('Choose playwright or cdp');
  const service = await WorkspaceService.open({ stateDir: join(directory, 'service'),
    config: loadServiceConfig(undefined, { browserChannel: process.env.FAWN_BROWSER_CHANNEL ?? 'chrome', headless }), createAdapter: factory });
  let controller, http;
  try {
    const workflowStore = workflowPersistence ? new WorkflowStore({ ...workflowPersistence,
      directory: join(directory, 'workflows') }) : null;
    controller = new WorkspaceController({ service, stateDir: directory, config, planner, workflowStore, workflowTasks });
    await controller.restore();
    http = await startWorkspaceServer({ controller, config, port });
    return { ...http, controller, service, async close() { await http.close(); await controller.close(); await service.close(); } };
  } catch (error) { controller?.store.close(); await service.close(); throw error; }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const args = process.argv.slice(2), value = name => { const index = args.indexOf(name); return index < 0 ? undefined : args[index + 1]; };
  if (args.includes('--help')) {
    console.log('Usage: npm --prefix browser/workspace start -- [--state-dir PATH] [--port NUMBER] [--adapter playwright|cdp] [--headless]');
    console.log('FAWN_BROWSER_CHANNEL selects stock Chrome/Chromium. FAWN_MODEL and ANTHROPIC_API_KEY enable explicit delegation. No agent starts automatically.');
  } else {
    const app = await openWorkspaceApp({ stateDir: value('--state-dir'), port: value('--port') === undefined ? undefined : Number(value('--port')),
      adapter: value('--adapter'), headless: args.includes('--headless') });
    console.log(`Fawn workspace: ${app.loginUrl}`);
    console.log('Start with human control. Model calls occur only after an explicit delegation.');
    let closing = false;
    const close = async () => { if (closing) return; closing = true; try { await app.close(); process.exitCode = 0; } catch (error) { console.error(error.message); process.exitCode = 1; } };
    process.on('SIGINT', close); process.on('SIGTERM', close);
  }
}
