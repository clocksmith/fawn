// Opt-in client trace cache. The workspace service remains the sole effect dispatcher.
// Complete traces publish atomically; unfinished, failed and uncertain runs teach nothing.
import { createHmac, randomBytes, randomUUID } from 'node:crypto';
import { canonicalJson } from '../journal.js';
import { structuralLocator, structuralOutlineHash } from '../adapters/page-query.js';

const VERSION = 1;
const HASH = /^[a-f0-9]{64}$/;
const KINDS = ['fill', 'select', 'click'];

function checkLimits(limits) {
  if (!limits || Object.keys(limits).sort().join() !== 'maxEdges,maxNodes,maxSteps'
    || Object.values(limits).some(value => !Number.isSafeInteger(value) || value < 1)) {
    throw Error('Trace limits require positive maxNodes, maxEdges and maxSteps');
  }
}

/** A bounded prefix DAG. Prefix identity prevents unchanged fill outlines creating cycles.
 * Limits are explicit caller policy. No model, credentials, observations or values are retained.
 */
export class TraceDag {
  constructor(limits) {
    checkLimits(limits);
    this.limits = Object.freeze({ ...limits });
    this.key = randomBytes(32).toString('hex');
    this.nodes = new Map();
  }
  hash(value) { return createHmac('sha256', this.key).update(canonicalJson(value)).digest('hex'); }
  export() {
    return structuredClone({ version: VERSION, key: this.key, limits: this.limits, nodes: [...this.nodes] });
  }
  /** Private generated state only. Loading never starts a session or dispatches an action. */
  static restore(state, limits) {
    const dag = new TraceDag(limits);
    if (state?.version !== VERSION || !HASH.test(state.key)
      || canonicalJson(state.limits) !== canonicalJson(limits) || !Array.isArray(state.nodes)
      || state.nodes.length > limits.maxNodes) throw Error('Invalid trace cache');
    const nodes = new Map();
    let edges = 0;
    for (const entry of state.nodes) {
      if (!Array.isArray(entry) || entry.length !== 2) throw Error('Invalid trace node');
      const [id, node] = entry;
      if (!HASH.test(id) || nodes.has(id) || !node || Object.keys(node).sort().join() !== 'depth,edges,terminal'
        || !Number.isSafeInteger(node.depth) || node.depth < 0 || node.depth > limits.maxSteps
        || typeof node.terminal !== 'boolean' || !Array.isArray(node.edges)) throw Error('Invalid trace node');
      if (edges + node.edges.length > limits.maxEdges) throw Error('Trace edge limit');
      for (const edge of node.edges) {
        if (!edge || Object.keys(edge).sort().join() !== 'next,step'
          || !HASH.test(edge.next) || !edge.step || Object.keys(edge.step).sort().join() !== 'kind,locator,slot'
          || !KINDS.includes(edge.step.kind) || !HASH.test(edge.step.locator)
          || (edge.step.kind === 'click' ? edge.step.slot !== null : !HASH.test(edge.step.slot))) throw Error('Invalid trace edge');
        edges++;
      }
      nodes.set(id, structuredClone(node));
    }
    if (edges > limits.maxEdges) throw Error('Trace edge limit');
    for (const node of nodes.values()) for (const edge of node.edges) {
      if (nodes.get(edge.next)?.depth !== node.depth + 1) throw Error('Trace must be acyclic');
    }
    dag.key = state.key;
    dag.nodes = nodes;
    return dag;
  }
  session(options) { return new TraceSession(this, options); }
  publish(records, terminal) {
    // Validate the entire merged graph before replacing the current cache.
    const nodes = new Map([...this.nodes].map(([id, node]) => [id, structuredClone(node)]));
    for (const { id, depth, step, next } of records) {
      const node = nodes.get(id) ?? { depth, edges: [], terminal: false };
      if (!node.edges.some(edge => canonicalJson(edge) === canonicalJson({ step, next }))) node.edges.push({ step, next });
      nodes.set(id, node);
    }
    const node = nodes.get(terminal.id) ?? { depth: terminal.depth, edges: [], terminal: false };
    node.terminal = true;
    nodes.set(terminal.id, node);
    this.nodes = TraceDag.restore({ ...this.export(), nodes: [...nodes] }, this.limits).nodes;
  }
}

/** Explicit session API for a controller/workflow, not model-provided policy.
 * intermediateClicks contains trusted structural locator tuples, never selectors.
 * Changing template purpose requires a new template string. Each session starts at depth zero.
 */
class TraceSession {
  constructor(dag, { service, owner, pageId, intent, template, inputs = {}, intermediateClicks = [], signal,
    assertContext = async () => {}, authorizeStep = async () => {}, accountScope = null }) {
    if (typeof template !== 'string' || !template || typeof pageId !== 'string' || !pageId
      || typeof intent !== 'string' || !intent) throw Error('Trace requires template, pageId and intent');
    this.dag = dag;
    this.service = service;
    this.identity = { ...owner, pageId };
    this.intent = intent;
    this.template = template;
    if (accountScope !== null && (typeof accountScope !== 'string' || !accountScope.trim())) throw Error('Invalid trace account scope');
    this.accountScope = accountScope;
    this.inputs = new Map(Object.entries(inputs).map(([slot, value]) => [dag.hash(['slot', slot]), value]));
    this.slotNames = new Map(Object.keys(inputs).map(slot => [dag.hash(['slot', slot]), slot]));
    this.assertContext = assertContext;
    this.authorizeStep = authorizeStep;
    this.clicks = new Set(intermediateClicks.map(locator => dag.hash(['locator', locator])));
    this.signal = signal;
    this.prefix = dag.hash(['start']);
    this.depth = 0;
    this.records = [];
    this.stopped = false;
    this.busy = false;
  }
  stop(reason, result) { this.stopped = true; return { status: 'stopped', reason, steps: this.depth, ...(result ? { result } : {}) }; }
  async exclusive(task) {
    if (this.busy || this.stopped) throw Error('Trace session is busy or stopped; no implicit resume');
    this.busy = true;
    try { this.signal?.throwIfAborted(); return await task(); }
    catch (error) { this.stopped = true; throw error; }
    finally { this.busy = false; }
  }
  async assertSettled() {
    this.signal?.throwIfAborted();
    await this.assertContext();
    const outstanding = await this.service.outstanding(this.identity);
    if (outstanding.some(action => action.isUncertain || action.dispatchStatus === 'queued'
      || (action.dispatchStatus === 'dispatched' && !action.outcome))) {
      throw Error('Resolve outstanding actions before recording or replaying a trace');
    }
  }
  frame(observation) {
    if (observation.isTruncated !== false || !Array.isArray(observation.elements)) throw Error('Trace requires a complete element observation');
    const scope = [this.template, new URL(observation.url).origin, observation.readScope ?? null];
    if (this.accountScope !== null) scope.push(this.accountScope);
    // Scope includes origin and read grants; raw requisition URLs and personal labels stay out.
    const id = this.dag.hash([scope, this.prefix, structuralOutlineHash(observation.elements)]);
    return { observation, id };
  }
  async observe() {
    await this.assertSettled();
    return this.frame(await this.service.observe(this.identity));
  }
  async execute(before, step) {
    if (this.depth >= this.dag.limits.maxSteps) return this.stop('step-limit');
    const matches = before.observation.elements.filter(element =>
      this.dag.hash(['locator', structuralLocator(element)]) === step.locator);
    if (matches.length !== 1) return this.stop(matches.length ? 'ambiguous-locator' : 'missing-locator');
    const element = matches[0];
    if (!element.label || element.formAction || ['submit', 'image', 'file', 'password', 'hidden'].includes(element.inputType)) return this.stop('unsupported-target');
    if (step.kind === 'click' && !this.clicks.has(step.locator)) return this.stop('intermediate-click-not-authorized');
    if (step.kind !== 'click' && (!this.inputs.has(step.slot) || typeof this.inputs.get(step.slot) !== 'string')) return this.stop('missing-input-slot');
    await this.assertContext();
    await this.authorizeStep({ origin: new URL(before.observation.url).origin, kind: step.kind,
      locator: structuralLocator(element), slot: this.slotNames.get(step.slot) ?? null });
    this.signal?.throwIfAborted();
    const admitted = await this.service.submitAction({ ...this.identity, actionId: randomUUID(),
      authorizedIntent: this.intent, observationVersion: before.observation.observationVersion,
      kind: step.kind, target: { handle: element.handle, selector: element.selector, digest: element.digest },
      ...(step.kind === 'click' ? {} : { params: { value: this.inputs.get(step.slot) } }) });
    const result = await admitted.result;
    if (result.outcome !== 'completed') return this.stop('action-not-completed', result);
    this.prefix = this.dag.hash([this.prefix, step]);
    this.depth++;
    const after = await this.observe();
    return { status: 'completed', result, after };
  }
  /** Slot provenance is structural: callers cannot pass a literal params.value at this API. */
  record(spec, observation = null) {
    return this.exclusive(async () => {
      if (!spec || Object.keys(spec).some(key => !['kind', 'locator', 'slot'].includes(key))
        || !KINDS.includes(spec.kind) || !Array.isArray(spec.locator) || spec.locator.length !== 4
        || spec.locator.some(value => typeof value !== 'string')
        || (spec.kind === 'click' ? spec.slot !== undefined : typeof spec.slot !== 'string')) return this.stop('uncacheable-step');
      const step = { kind: spec.kind, locator: this.dag.hash(['locator', spec.locator]),
        slot: spec.kind === 'click' ? null : this.dag.hash(['slot', spec.slot]) };
      // A planner must execute against the observation it actually saw, not silently rebind
      // its decision after the page changes. Cache replay still resolves a fresh live target.
      await this.assertSettled();
      const before = observation ? this.frame(observation) : await this.observe();
      if (this.records.length && this.records.at(-1).next !== before.id) return this.stop('unseen-branch');
      const depth = this.depth;
      const executed = await this.execute(before, step);
      if (executed.status === 'completed') this.records.push({ id: before.id, depth, step, next: executed.after.id });
      return { status: executed.status, reason: executed.reason, result: executed.result };
    });
  }
  /** Call only after the task's pre-submit acceptance checks pass. No final action is cached. */
  finish() {
    return this.exclusive(async () => {
      const terminal = await this.observe();
      if (!this.records.length || this.records.at(-1).next !== terminal.id) return this.stop('changed-terminal');
      this.dag.publish(this.records, { id: terminal.id, depth: this.depth });
      return this.stop('recorded-submit-boundary');
    });
  }
  replay() {
    return this.exclusive(async () => {
      let before = await this.observe();
      for (;;) {
        const node = this.dag.nodes.get(before.id);
        if (!node) return this.stop('unseen-branch');
        if (node.terminal) return this.stop('submit-boundary');
        const steps = new Map(node.edges.map(edge => [canonicalJson(edge.step), edge.step]));
        if (steps.size !== 1) return this.stop('ambiguous-branch');
        const step = steps.values().next().value;
        const executed = await this.execute(before, step);
        if (executed.status !== 'completed') return executed;
        if (!node.edges.some(edge => canonicalJson(edge.step) === canonicalJson(step) && edge.next === executed.after.id)) return this.stop('unseen-branch');
        before = executed.after;
      }
    });
  }
}
