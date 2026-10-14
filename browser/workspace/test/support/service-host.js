#!/usr/bin/env node
// Fawn workspace tests — service host process driven over stdin/stdout JSON lines.
//
// Lets tests kill the real service process at declared points and reopen its state directory.
// Usage: node service-host.js '{"stateDir": "...", "adapter": "fake"|"playwright", ...}'

import { createInterface } from 'node:readline';
import { loadServiceConfig } from '../../src/config.js';
import { WorkspaceService } from '../../src/service.js';
import { createOwnerSession } from '../../src/workflows/owner-session.js';
import { runArtifactExport } from '../../src/workflows/artifact-export.js';
import { runDocumentResearch } from '../../src/workflows/document-research.js';
import { runFormSubmission } from '../../src/workflows/form-submission.js';
import { FakeAdapter } from './fake-adapter.js';

const options = JSON.parse(process.argv[2]);

function emit(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function crashHooks(crash) {
  if (!crash) return {};
  return {
    [crash.hook]: (request) => {
      if (request.actionId.endsWith(crash.actionIdSuffix)) process.kill(process.pid, 'SIGKILL');
    },
  };
}

const BROWSER_ADAPTERS = Object.freeze({
  playwright: async () => (await import('../../src/adapters/playwright-adapter.js')).createPlaywrightAdapter,
  cdp: async () => (await import('../../src/adapters/cdp-adapter.js')).createCdpAdapter,
});

async function adapterFactory() {
  if (options.adapter === 'fake') return () => new FakeAdapter({ receiptsPath: options.receiptsPath });
  const load = BROWSER_ADAPTERS[options.adapter];
  if (!load) throw new Error(`unknown adapter ${options.adapter}`);
  return load();
}

const WORKFLOWS = Object.freeze({
  research: runDocumentResearch,
  form: runFormSubmission,
  export: runArtifactExport,
});

let service;
try {
  service = await WorkspaceService.open({
    stateDir: options.stateDir,
    config: loadServiceConfig(),
    createAdapter: await adapterFactory(),
    hooks: crashHooks(options.crash),
  });
} catch (error) {
  emit({ event: 'open-failed', error: { code: error.code ?? null, message: error.message } });
  process.exit(1);
}

// Test-only command faults: the workflow observes normally but its mutations carry stale state.
const SESSION_FAULTS = Object.freeze({
  'stale-observation': (session) => ({
    ...session,
    submit: (observation, spec) => session.submit({ ...observation, observationVersion: observation.observationVersion - 1 }, spec),
  }),
  'stale-owner': (session, fault) => ({ ...session, submit: createOwnerSession(service, fault.owner).submit }),
});

function sessionFor(owner, fault) {
  const session = createOwnerSession(service, owner);
  return fault ? SESSION_FAULTS[fault.kind](session, fault) : session;
}

const OPERATIONS = Object.freeze({
  call: ({ method, args }) => service[method](args),
  submitAndWait: async (args) => (await service.submitAction(args)).result,
  workflow: ({ name, owner, task, fault }) => WORKFLOWS[name](sessionFor(owner, fault), task),
  close: async () => {
    await service.close();
    setImmediate(() => process.exit(0));
    return null;
  },
});

emit({ event: 'ready' });
for await (const line of createInterface({ input: process.stdin })) {
  const { id, op, args } = JSON.parse(line);
  try {
    emit({ id, ok: true, result: await OPERATIONS[op](args) });
  } catch (error) {
    emit({ id, ok: false, error: { code: error.code ?? null, message: error.message } });
  }
}

// The parent closed stdin (it exited or crashed): release the browser and journal.
await service.close();
process.exit(0);
