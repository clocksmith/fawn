import assert from 'node:assert/strict';
import { canonicalJson, sha256Hex } from '../../src/journal.js';
import { taskContract, readOnlyPlanner, validateHandoff } from '../../src/client/portable-handoff.js';

const locator = ['textbox', 'Filter', 'input', 'text'];
const definition = {
  disclosurePolicy: {
    origin: 'https://fixture.invalid', fields: [{ kind: 'fill', slot: 'term', locator }],
    intermediateClicks: [['button', 'Choose', 'button', '']],
  },
  taskSourceHashes: { fixture: sha256Hex('version-one') },
  verificationId: 'filter-v1',
  verifyReadOnlyHandoffStep: async () => true,
};
const template = 'filter';
const body = {
  schema: 'blitzen.workspace-handoff/v1', source: sha256Hex('private source'),
  evidence: [], unresolved: [],
  routine: { contract: taskContract(template, definition), revision: 1, nodes: [
    { terminal: false, repairs: [], steps: [{ rule: 0, kind: 'fill', next: 1 }] },
    { terminal: true, repairs: [], steps: [] },
  ] },
};
function artifact(change = () => {}) {
  const value = structuredClone(body);
  change(value);
  return { ...value, digest: sha256Hex(canonicalJson(value)) };
}
const valid = validateHandoff(artifact());
const planner = readOnlyPlanner(valid, template, definition);
const element = { handle: 'new-local-handle', role: 'textbox', label: 'Filter', tag: 'input', inputType: 'text' };
const context = { observation: { elements: [element] }, inputs: { term: 'local' } };
assert.deepEqual(planner.next(context), { tool: 'act', input: { kind: 'fill', handle: element.handle, slot: 'term' } });
assert.throws(() => planner.next({ ...context, lastResult: { status: 'needs-replan' } }), /unconfirmed/);
assert.throws(() => readOnlyPlanner(valid, template, definition).next({ ...context, inputs: {} }), /provide every/);
assert.throws(() => readOnlyPlanner(valid, template, definition).next({ ...context, observation: { elements: [element, element] } }), /one matching field/);
assert.throws(() => readOnlyPlanner(valid, template, { ...definition, verificationId: 'different' }), /matching task/);
assert.throws(() => readOnlyPlanner(valid, template, { ...definition, taskSourceHashes: { fixture: sha256Hex('changed') } }), /matching task/);
assert.throws(() => validateHandoff({ ...valid, digest: sha256Hex('other') }), /digest mismatch/);
assert.throws(() => validateHandoff(artifact(a => { a.routine.nodes[0].steps[0].next = 0; })), /acyclic/);
assert.throws(() => validateHandoff(artifact(a => { a.evidence.push({ text: 'raw' }); })), /evidence references/);
const clicks = validateHandoff(artifact(a => { a.routine.nodes[0].steps[0] = { rule: 1, kind: 'click', next: 1 }; }));
assert.throws(() => readOnlyPlanner(clicks, template, definition), /filter fields/);
const branched = validateHandoff(artifact(a => { a.routine.nodes[0].steps.push({ rule: 0, kind: 'fill', next: 1 }); }));
assert.throws(() => readOnlyPlanner(branched, template, definition), /unambiguous/);
const unresolved = validateHandoff(artifact(a => { a.unresolved.push({ ref: sha256Hex('source action'), kind: 'submit' }); }));
assert.throws(() => readOnlyPlanner(unresolved, template, definition), /source workspace/);
console.log('ok: portable routine schema, independent contract, bounded structures and read-only dispatch restrictions');
