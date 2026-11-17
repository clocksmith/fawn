// An agent that saves a page as evidence and then acts must not lose a planner request to a
// stale-observation refusal: capture reads the page again, and that read becomes the current one.
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { WorkspaceService } from '../../src/service.js';
import { loadServiceConfig } from '../../src/config.js';
import { WorkspaceController } from '../../src/client/controller.js';
import { FakeAdapter, ORIGIN } from '../support/fake-adapter.js';

const config = { ...JSON.parse(readFileSync(new URL('../../../../config/fawn-workspace-client.json', import.meta.url))), maxTurns: 5 };
const root = mkdtempSync(join(tmpdir(), 'fawn-capture-current-'));
let service, controller;
try {
  service = await WorkspaceService.open({ stateDir: join(root, 'service'), config: loadServiceConfig(),
    createAdapter: () => new FakeAdapter({ receiptsPath: join(root, 'receipts') }) });
  const steps = [
    { tool: 'observe', input: { pageId: 'main' } },
    { tool: 'capture', input: { pageId: 'main' } },
    { tool: 'act', input: { pageId: 'main', kind: 'navigate', target: { url: `${ORIGIN}/form/done` } } },
    { tool: 'finish', input: { summary: 'Saved the page, then moved on.' } },
  ];
  let turn = 0;
  controller = new WorkspaceController({ service, stateDir: join(root, 'client'), config,
    planner: { identity: { model: 'scripted' }, next: async () => steps[turn++] } });
  const { id } = await controller.create({ goal: 'Save a page and continue' });
  await controller.navigate(id, { pageId: 'main', url: `${ORIGIN}/form` });
  await controller.delegate(id, { pageIds: ['main'], origins: [ORIGIN], actionKinds: ['navigate'], task: 'Save the form page, then open the done page.' });
  await controller.active.get(id)?.done;
  const project = controller.store.get(id);
  assert.deepEqual(project.activity.filter(event => event.event === 'tool-failed').map(event => event.error), [], 'no proposal is refused');
  assert.equal(project.run.status, 'completed', JSON.stringify(project.run));
  assert.equal(turn, 4, 'four planner requests, none spent on a refusal');
  const navigated = project.activity.find(event => event.event === 'tool-result' && event.output?.outcome);
  assert.equal(navigated.output.outcome, 'completed');
  console.log('ok: capture keeps the agent observation current; the following navigation is admitted without an extra request');
} finally {
  await controller?.close().catch(() => {}); await service?.close().catch(() => {});
  rmSync(root, { recursive: true, force: true });
}
