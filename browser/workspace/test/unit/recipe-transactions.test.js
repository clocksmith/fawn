import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { canonicalJson } from '../../src/client/trace-graph-dp.js';
import worker from '../../src/recipe-service/worker.js';
import { createManagedRecipeEngine } from '../../src/recipe-service/engine.js';
import { createSqliteRecipeStore } from '../../src/recipe-service/store-sqlite.js';

const store = createSqliteRecipeStore();
const engine = createManagedRecipeEngine({ store, secret: 'a'.repeat(32) });
const principal = { workspaceId: 'alpha', clientId: 'client', role: 'member' };
const trace = {
  submissionId: 'trace-001', verificationId: 'review-1', reviewVerified: true,
  scope: 'catalog', origin: 'https://example.com', template: 'save',
  steps: [{ kind: 'click', locator: ['button', 'Save', 'button', ''],
    beforeOutline: 'a'.repeat(64), afterOutline: 'b'.repeat(64) }],
};
const submit = (body, who = principal) => engine.ingestTrace(who, body);
try {
  await store.run(`CREATE TEMP TRIGGER fail_receipt BEFORE INSERT ON recipe_trace_receipts
    BEGIN SELECT RAISE(ABORT, 'receipt write failed'); END`);
  await assert.rejects(submit(trace), /receipt write failed/);
  for (const table of ['recipe_namespaces', 'recipe_edges', 'recipe_edge_authors', 'recipe_nodes']) {
    assert.equal((await store.get(`SELECT COUNT(*) AS count FROM ${table}`)).count, 0);
  }
  await store.run('DROP TRIGGER fail_receipt');
  const batch = store.batch;
  let loseCommitResponse = true;
  store.batch = async (statements) => {
    const result = await batch(statements);
    if (loseCommitResponse && statements.some(({ sql }) => sql.startsWith('INSERT INTO recipe_trace_receipts'))) {
      loseCommitResponse = false;
      throw Error('lost response after commit');
    }
    return result;
  };
  const [first, duplicate] = await Promise.all([submit(trace), submit(trace)]);
  assert.equal(loseCommitResponse, false);
  assert.deepEqual(await submit(trace), first);
  assert.deepEqual(duplicate, first);
  assert.equal(first.revision, 1);
  await assert.rejects(submit({ ...trace, verificationId: 'other' }), { status: 409 });
  await assert.rejects(submit({ ...trace, steps: [{ ...trace.steps[0], elapsedMs: 200 }] }), { status: 409 });
  const second = { ...principal, workspaceId: 'beta' };
  await assert.rejects(submit({ ...trace, steps: [] }, second), { status: 400 });
  const receipts = await Promise.all([submit(trace, second), submit({ ...trace, submissionId: 'trace-003' })]);
  assert.deepEqual(receipts.map(r => r.revision).sort(), [2, 3]);
  assert.deepEqual(await store.get('SELECT attempts, author_count, promoted FROM recipe_edges'),
    { attempts: 3, author_count: 2, promoted: 1 });
  await assert.rejects(submit({ ...trace, steps: [] }), { status: 409, code: 'submission-conflict' });
  await assert.rejects(submit({ ...trace, scope: 'workspace' }), { status: 409, code: 'submission-conflict' });
  await assert.rejects(submit({ ...trace, template: 'other' }), { status: 409, code: 'submission-conflict' });
  const race = { ...trace, submissionId: 'scope-race', origin: 'https://other.example.com' };
  const outcomes = await Promise.allSettled([submit(race), submit({ ...race, scope: 'workspace' })]);
  assert.equal(outcomes.filter(result => result.status === 'fulfilled').length, 1);
  assert.equal(outcomes.find(result => result.status === 'rejected').reason.code, 'submission-conflict');
  assert.deepEqual(await store.get('SELECT COUNT(*) AS count FROM recipe_namespaces WHERE origin = ?', [race.origin]),
    { count: 1 });
  const workspace = await submit({ ...trace, scope: 'workspace', submissionId: 'workspace-trace' });
  assert.equal(workspace.revision, 1);
  const previous = { ...trace, scope: 'workspace', submissionId: 'previous-encoding' };
  const oldSteps = [{ ...trace.steps[0], slot: null, beforeSlots: null, beforeClicks: null, elapsedMs: 0, outcome: 'completed' }];
  const oldLimits = { maxNodes: 256, maxEdges: 512, maxSteps: 32 };
  const authorHash = createHash('sha256').update(canonicalJson(['author-v1', principal.workspaceId, principal.clientId])).digest('hex');
  const requestHash = createHash('sha256').update(canonicalJson([trace.verificationId, oldSteps, oldLimits])).digest('hex');
  await store.run('INSERT INTO recipe_submission_receipts VALUES (?, ?, ?, ?, ?, ?)',
    [workspace.namespaceId, authorHash, previous.submissionId, trace.verificationId, requestHash, '{"revision":5}']);
  assert.deepEqual(await submit(previous), { revision: 5 });
  await assert.rejects(submit({ ...previous, scope: 'catalog' }), { status: 409, code: 'submission-conflict' });
  const legacy = { ...trace, submissionId: 'pre-upgrade', scope: 'workspace', steps: [] };
  await store.run('INSERT INTO recipe_submissions VALUES (?, ?, ?, ?, ?, ?)',
    [legacy.submissionId, workspace.namespaceId, principal.clientId, trace.verificationId, '{"revision":7}', 'legacy']);
  await assert.rejects(submit(legacy), { status: 409, code: 'legacy-submission' });
  await assert.rejects(submit({ ...legacy, steps: trace.steps }), { status: 409 });
} finally {
  store.close();
}

// A reused Worker isolate must honor a rotated signing secret and a new DB binding.
function emptyDatabase(revision) {
  return {
    prepare(sql) { return { sql, bind() { return this; } }; },
    async batch(queries) {
      return queries.map(({ sql }) => ({ results: sql.startsWith('SELECT revision') ? [{ revision }] : [] }));
    },
  };
}
const token = 'T'.repeat(32);
const env = {
  DB: emptyDatabase(2), FAWN_RECIPE_SECRET: 'A'.repeat(32),
  FAWN_RECIPE_CLIENTS_JSON: JSON.stringify([{ ...principal, tokenHash: createHash('sha256').update(token).digest('hex') }]),
};
async function resolve(bindings) {
  const res = await worker.fetch(new Request('https://service/v1/recipes/resolve', {
    method: 'POST', headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' },
    body: JSON.stringify({ origin: trace.origin, template: trace.template }),
  }), bindings);
  assert.equal(res.status, 200);
  return res.json();
}
const original = await resolve(env);
const rotated = await resolve({ ...env, FAWN_RECIPE_SECRET: 'B'.repeat(32) });
assert.notEqual(original.signature, rotated.signature);
assert.equal((await resolve({ ...env, DB: emptyDatabase(3) })).revision, 3);
