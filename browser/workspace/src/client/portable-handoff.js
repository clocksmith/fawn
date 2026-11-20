import { canonicalJson, sha256Hex } from '../journal.js';
import { structuralLocator } from '../adapters/page-query.js';
import { topologicalSort } from './trace-graph-dp.js';

const SCHEMA = 'blitzen.workspace-handoff/v1';
const HASH = /^[0-9a-f]{64}$/;
const KINDS = ['navigate', 'fill', 'select', 'click', 'submit', 'download', 'upload'];
const MAX_RECORDS = 1024;
const hash = value => sha256Hex(canonicalJson(value));
const check = (ok, message) => { if (!ok) throw Error(message); };
const keys = (obj, expected) => obj && typeof obj === 'object' && !Array.isArray(obj)
  && Object.keys(obj).length === expected.length && expected.every(key => Object.hasOwn(obj, key));
const digest = v => typeof v === 'string' && HASH.test(v);
const list = v => Array.isArray(v) && v.length <= MAX_RECORDS;
const index = (n, size) => Number.isSafeInteger(n) && n >= 0 && n < size;

export function taskContract(template, definition) {
  // Accounts bind independently. Equal public task semantics allow local interpretation of rule indexes.
  return hash([template, definition.disclosurePolicy, definition.verificationId, definition.taskSourceHashes, definition.fieldGroups ?? null]);
}

export function portableGraph(dag, revision, template, definition) {
  const indexes = new Map([...dag.nodes.keys()].map((id, i) => [id, i]));
  const policy = definition.disclosurePolicy;
  const rules = [...policy.fields, ...policy.intermediateClicks.map(locator => ({ kind: 'click', locator }))];
  const ruleByStep = new Map(rules.map((rule, i) => [canonicalJson({ kind: rule.kind,
    locator: dag.hash(['locator', rule.locator]), slot: rule.kind === 'click' ? null : dag.hash(['slot', rule.slot]) }), i]));
  const nodes = [...dag.nodes.values()].map(node => ({ terminal: node.terminal,
    repairs: node.repairs.map(id => indexes.get(id)), steps: node.edges.map(({ next, step }) => {
      const rule = ruleByStep.get(canonicalJson(step));
      check(rule !== undefined, 'An earlier learned step no longer matches the installed disclosure policy');
      return { kind: step.kind, rule, next: indexes.get(next) };
    }) }));
  return { contract: taskContract(template, definition), revision, nodes };
}

export function exportHandoff(project, actions, evidenceIds, routine) {
  check(list(evidenceIds) && new Set(evidenceIds).size === evidenceIds.length, 'Select distinct evidence IDs');
  const source = hash([SCHEMA, project.id]);
  const evidence = evidenceIds.map(id => {
    const a = Object.hasOwn(project.artifacts, id) && project.artifacts[id];
    check(a && a.kind === 'evidence' && digest(a.source?.sha256), 'Select saved evidence from this investigation');
    return { ref: hash([source, id]), revision: a.revision, sha256: a.source.sha256, status: a.status };
  });
  const unresolved = actions.filter(action => action.isUncertain || action.dispatchStatus === 'queued'
    || (action.dispatchStatus === 'dispatched' && !action.outcome))
    .map(action => ({ ref: hash([source, action.actionId]), kind: action.kind }));
  const body = { schema: SCHEMA, source, evidence, routine, unresolved };
  return validateHandoff({ ...body, digest: hash(body) });
}

export function validateHandoff(value) {
  check(keys(value, ['schema','source','evidence','routine','unresolved','digest']) && value.schema === SCHEMA
    && digest(value.source) && digest(value.digest), 'Invalid portable handoff');
  const { evidence, routine, unresolved } = value;
  check(list(evidence) && evidence.every(e => keys(e,['ref','revision','sha256','status']) && digest(e.ref) && digest(e.sha256)
    && Number.isSafeInteger(e.revision) && e.revision > 0 && ['current','needs-review'].includes(e.status))
    && new Set(evidence.map(e=>e.ref)).size === evidence.length, 'Invalid evidence references');
  check(list(unresolved) && unresolved.every(a => keys(a,['ref','kind']) && digest(a.ref) && KINDS.includes(a.kind))
    && new Set(unresolved.map(a=>a.ref)).size === unresolved.length, 'Invalid source action references');
  if (routine !== null) {
    check(keys(routine,['contract','revision','nodes']) && digest(routine.contract) && Number.isSafeInteger(routine.revision)
      && routine.revision > 0 && list(routine.nodes) && routine.nodes.length > 0, 'Invalid learned routine');
    const n = routine.nodes.length;
    let edges = 0;
    const topology = new Map();
    for (const [i,node] of routine.nodes.entries()) {
      check(keys(node,['terminal','steps','repairs']) && typeof node.terminal === 'boolean' && list(node.repairs) && list(node.steps)
        && node.repairs.every(to => index(to,n)) && node.steps.every(s => keys(s,['kind','rule','next'])
          && ['fill','select','click'].includes(s.kind) && index(s.rule,MAX_RECORDS) && index(s.next,n)), 'Invalid routine edge');
      edges += node.repairs.length + node.steps.length;
      check(edges <= MAX_RECORDS && (!node.terminal || node.repairs.length === 0), 'Invalid routine structure');
      topology.set(String(i), { repairs: node.repairs.map(String), edges: node.steps.map(s=>({next:String(s.next)})) });
    }
    check(edges > 0 && routine.nodes.some(n => n.terminal), 'No verified review boundary in routine');
    topologicalSort(topology, MAX_RECORDS);
  }
  const { digest: expected, ...body } = value;
  check(hash(body) === expected, 'Portable handoff digest mismatch');
  return value;
}


export function readOnlyPlanner(snapshot, template, definition) {
  const graph = snapshot.routine;
  check(graph?.contract === taskContract(template, definition) && typeof definition.verifyReadOnlyHandoffStep === 'function',
    'Recipient must install a matching task with an independent read-only step verifier');
  check(snapshot.unresolved.length === 0, 'Resolve outstanding effects in the source workspace before using its routine');
  const incoming = new Set(graph.nodes.flatMap(node => node.steps.map(step => step.next)));
  const starts = graph.nodes.map((_,i) => i).filter(i => !incoming.has(i));
  check(starts.length === 1 && graph.nodes.every(n => !n.repairs.length && n.steps.length === (n.terminal ? 0 : 1)),
    'Only a single unambiguous learned path can be used');
  const fields = definition.disclosurePolicy.fields;
  const steps = [];
  let node = graph.nodes[starts[0]];
  while (!node.terminal) {
    const step = node.steps[0], field = fields[step.rule];
    check(field && ['fill','select'].includes(step.kind) && field.kind === step.kind,
      'Portable use only permits locally reviewed filter fields');
    steps.push(field); node = graph.nodes[step.next];
  }
  check(steps.length + 1 === graph.nodes.length, 'Every step must belong to the selected path');
  let cursor = 0;
  return { identity: { kind: 'local-handoff' }, kinds: [...new Set(steps.map(s => s.kind))],
    next({ observation, lastResult, inputs }) {
      if (cursor > 0 && lastResult?.status !== 'completed') throw Error('Portable routine stopped before advancing past an unconfirmed step');
      if (cursor === steps.length) return { tool: 'finish', input: { summary: 'Verify recipient result' } };
      const field = steps[cursor++];
      check(Object.hasOwn(inputs,field.slot), 'Recipient must provide every selected field value');
      const matches = observation.elements.filter(e => canonicalJson(structuralLocator(e)) === canonicalJson(field.locator));
      check(matches.length === 1, 'Recipient page does not have one matching field');
      return { tool: 'act', input: { kind: field.kind, handle: matches[0].handle, slot: field.slot } };
    } };
}
