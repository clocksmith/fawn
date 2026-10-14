// Real Chromium: research, form-submission and export workflows through one service, identical
// stale-command rejection, and an external submission interrupted by a process kill.
import assert from 'node:assert/strict';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { startFixtureServer } from '../fixtures/fixture-server.js';
import { startHost } from '../support/service-client.js';

const WORKSPACE_ID = 'ws-browser';
const ADAPTERS = Object.freeze(['playwright', 'cdp']);
const INTENTS = Object.freeze({ research: 'research:read', form: 'form:contact', export: 'export:report' });
const GRANT_KINDS = Object.freeze({
  research: ['navigate', 'click'],
  form: ['navigate', 'fill', 'submit'],
  export: ['navigate', 'download'],
});

let killOnSubmission = null;
const fixture = await startFixtureServer({
  onSubmission: async (receipt) => {
    if (killOnSubmission && receipt.fields.name === killOnSubmission.name) await killOnSubmission.host.kill();
  },
});
const origin = fixture.origin;

function identity(owner) {
  const { workspaceId, ownerId, ownerGeneration, ownerCredential } = owner;
  return { workspaceId, ownerId, ownerGeneration, ownerCredential };
}

async function rejectsWith(promise, code) {
  await assert.rejects(promise, (error) => {
    assert.equal(error.code, code, error.message);
    return true;
  });
}

function tasks(runId, origin) {
  return {
    research: {
      runId: `${runId}-research`,
      intent: INTENTS.research,
      indexUrl: `${origin}/research/`,
      topics: ['vulkan'],
      maxDocuments: 5,
    },
    form: {
      runId: `${runId}-form`,
      intent: INTENTS.form,
      formUrl: `${origin}/form`,
      fields: { name: `Ada ${runId}`, email: 'ada@example.test' },
      completedWhen: [{ kind: 'element-present', selector: '#receipt' }],
      failedWhen: [{ kind: 'element-present', selector: '#error' }],
    },
    export: {
      runId: `${runId}-export`,
      intent: INTENTS.export,
      exportUrl: `${origin}/export`,
      linkText: 'Download report',
    },
  };
}

async function scenario(adapter) {
  const stateDir = join(mkdtempSync(join(tmpdir(), `fawn-browser-${adapter}-`)), 'state');
  const base = { stateDir, adapter };
  const grace = `Grace run-3-${adapter}`;
  const lostActionId = `run-3-${adapter}-form-submit`;
  let host = await startHost(base);
  const { adminCredential } = await host.call('createWorkspace', { workspaceId: WORKSPACE_ID });
  for (const ownerId of ['agent-a', 'agent-b']) {
    for (const [name, intent] of Object.entries(INTENTS)) {
      await host.call('grantAuthorization', {
        workspaceId: WORKSPACE_ID,
        adminCredential,
        grantId: `${ownerId}-${name}`,
        ownerId,
        intents: [intent],
        actionKinds: GRANT_KINDS[name],
        targetOrigins: [origin],
      });
    }
  }
  const assigned = await host.call('assignOwner', { workspaceId: WORKSPACE_ID, adminCredential, ownerId: 'agent-a' });
  const ownerA = { workspaceId: WORKSPACE_ID, ...assigned };

  // The three workflow adapters complete through the same service.
  const run = tasks(`run-1-${adapter}`, origin);
  const research = await host.workflow('research', ownerA, run.research);
  assert.equal(research.status, 'completed', JSON.stringify(research.steps.at(-1)));
  assert.deepEqual(research.findings.map((finding) => finding.title), ['Vulkan memory model', 'Vulkan synchronization']);
  assert.ok(research.findings.every((finding) => finding.passages.some((passage) => passage !== finding.title)));
  assert.equal(research.findings.some((finding) => finding.passages.join(' ').includes('Sourdough')), false);
  const exported = await host.workflow('export', ownerA, run.export);
  assert.equal(exported.status, 'completed', JSON.stringify(exported.steps.at(-1)));
  assert.equal(exported.artifact.sha256, fixture.reportSha256);
  const submitted = await host.workflow('form', ownerA, run.form);
  assert.equal(submitted.status, 'completed', JSON.stringify(submitted.submission));
  assert.equal(fixture.receipts.filter((receipt) => receipt.fields.name === `Ada run-1-${adapter}`).length, 1);
  const invalid = await host.workflow('form', ownerA, {
    ...tasks(`run-2-${adapter}`, origin).form,
    fields: { name: `Bad run-2-${adapter}`, email: 'not-an-address' },
  });
  assert.equal(invalid.status, 'failed');
  assert.equal(invalid.submission.outcomeBasis, 'failure-condition');

  // Every workflow adapter receives the same rejection for the same stale command.
  const staleObservation = { kind: 'stale-observation' };
  for (const [name, task] of Object.entries(tasks(`stale-observation-${adapter}`, origin))) {
    await rejectsWith(host.workflow(name, ownerA, task, staleObservation), 'stale-observation');
  }
  const handoff = await host.call('assignOwner', { workspaceId: WORKSPACE_ID, adminCredential, ownerId: 'agent-b' });
  const ownerB = { workspaceId: WORKSPACE_ID, ...handoff };
  const staleOwner = { kind: 'stale-owner', owner: identity(ownerA) };
  for (const [name, task] of Object.entries(tasks(`stale-owner-${adapter}`, origin))) {
    await rejectsWith(host.workflow(name, ownerB, task, staleOwner), 'stale-owner-generation');
  }
  await host.close();

  // Restart: the old generation stays stale; the persistent profile reopens.
  host = await startHost(base);
  await rejectsWith(host.call('observe', identity(ownerA)), 'stale-owner-generation');
  const crashTask = { ...tasks(`run-3-${adapter}`, origin).form, fields: { name: grace, email: 'grace@example.test' } };
  killOnSubmission = { name: grace, host };
  await assert.rejects(host.workflow('form', ownerB, crashTask), (error) => error.exit?.signal === 'SIGKILL');
  killOnSubmission = null;
  assert.equal(fixture.receipts.filter((receipt) => receipt.fields.name === grace).length, 1);

  // After the kill the submission is unknown; no resubmission until reconciliation.
  host = await startHost(base);
  const lost = await host.call('action', { ...identity(ownerB), actionId: lostActionId });
  assert.equal(lost.outcome, 'unknown');
  assert.equal(lost.outcomeBasis, 'restart-after-dispatch-start');
  assert.equal(lost.isUncertain, true);
  await rejectsWith(host.workflow('form', ownerB, { ...crashTask, runId: `run-4-${adapter}-form`, resubmissionOf: lostActionId }),
    'effect-unresolved');
  const before = await host.call('observe', identity(ownerB));
  const toSubmissions = await host.submitAndWait({
    ...identity(ownerB),
    actionId: `run-5-${adapter}-open-submissions`,
    authorizedIntent: INTENTS.form,
    kind: 'navigate',
    target: { url: `${origin}/submissions` },
    observationVersion: before.observationVersion,
  });
  assert.equal(toSubmissions.outcome, 'completed');
  const reconciled = await host.call('reconcile', {
    ...identity(ownerB),
    actionId: lostActionId,
    method: 'application-state',
    effectPresentWhen: [{ kind: 'element-text-includes', selector: '#submissions', value: `${grace} grace@example.test` }],
  });
  assert.equal(reconciled.reconciliation.result, 'effect-present');
  assert.equal(reconciled.outcome, 'unknown');
  await rejectsWith(host.workflow('form', ownerB, { ...crashTask, runId: `run-6-${adapter}-form`, resubmissionOf: lostActionId }),
    'effect-already-completed');
  assert.equal(fixture.receipts.filter((receipt) => receipt.fields.name === grace).length, 1);
  await host.close();
}

for (const adapter of ADAPTERS) {
  await scenario(adapter);
  console.log(`ok: browser workflows (${adapter})`);
}
await fixture.close();
