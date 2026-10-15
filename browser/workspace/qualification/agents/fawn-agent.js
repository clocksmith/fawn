// Fawn workspace differentiation — treatment tools: the agent as a client of the workspace service.
//
// Observations use the outline view with deltas against the agent's previous version; unchanged
// regions come from the agent's own cache. Each action cites the observation its decision used
// and binds staleness to the regions that decision read. A handoff makes the agent wait for
// ownership and observe again before acting.

const OWNERSHIP_POLL_MS = 50;
const OWNERSHIP_WAIT_MS = 30000;
const OUTCOME_POLL_MS = 50;
const OUTCOME_WAIT_MS = 20000;
const REDECIDE_CODES = Object.freeze(['stale-observation', 'target-changed', 'precondition-unestablished', 'expired-observation']);
const OWNERSHIP_CODES = Object.freeze(['stale-owner-generation', 'unauthenticated']);

const VIEWS = Object.freeze({
  catalog: { regionGroups: ['.product-row'], regions: ['#pager', '#cart-summary'] },
  cart: { regionGroups: ['.cart-row'], regions: ['#cart-total'] },
  checkout: { regions: ['#order-form'] },
  confirmation: { regions: ['h1', '#invoice'] },
});

const MONEY = /\$(\d+)\.(\d{2})/;
const HANDLE = /\[(h[0-9a-z-]+) (\w+) "([^"]*)"(?: = ("(?:[^"\\]|\\.)*"))?\]/g;

function cents(text) {
  const match = MONEY.exec(text);
  return match ? Number(match[1]) * 100 + Number(match[2]) : null;
}

function controls(outline) {
  return [...outline.matchAll(HANDLE)].map(([, handle, role, name, value]) => ({
    handle,
    role,
    name,
    value: value === undefined ? null : JSON.parse(value),
  }));
}

const PARSERS = Object.freeze({
  catalog: (regions, version) => ({
    items: Object.entries(regions).filter(([selector]) => selector.startsWith('#row-')).map(([selector, outline]) => {
      const [name, price, stock] = outline.split(' | ');
      return {
        name,
        price: cents(price),
        stock: stock === 'out of stock' ? 0 : Number.parseInt(stock, 10),
        handle: controls(outline).find((control) => control.name === 'Add to cart')?.handle,
        region: selector,
        version,
      };
    }),
  }),
  cart: (regions, version) => ({
    lines: Object.entries(regions).filter(([selector]) => selector.startsWith('#cart-') && selector !== '#cart-total')
      .map(([selector, outline]) => ({
        name: outline.split(' | ')[0],
        price: cents(outline),
        handle: controls(outline).find((control) => control.name === 'Remove')?.handle,
        region: selector,
        version,
      })),
  }),
  checkout: (regions, version) => {
    const fields = controls(regions['#order-form'] ?? '');
    const field = (name) => {
      const control = fields.find((candidate) => candidate.name === name);
      return control ? { handle: control.handle, value: control.value ?? '', version } : null;
    };
    return {
      address: field('address'),
      reference: field('reference'),
      place: { handle: fields.find((control) => control.name === 'Place order')?.handle, version },
    };
  },
  confirmation: (regions, version) => {
    const link = controls(regions['#invoice'] ?? '').find((control) => control.name === 'Download invoice');
    return { orderId: Number(/Order (\d+) placed/.exec(regions.h1 ?? '')?.[1]) || null, invoice: link ? { handle: link.handle, version } : null };
  },
});

class AgentKilled extends Error {
  constructor() {
    super('agent process ended');
    this.code = 'agent-killed';
  }
}

class Redecide extends Error {
  constructor(code) {
    super(`refused: ${code}`);
    this.code = code;
    this.redecide = true;
  }
}

class FawnAgent {
  constructor({ host, identity, checkpoint, thinkTimeMs, task }) {
    this.host = host;
    this.identity = identity;
    this.checkpoint = checkpoint;
    this.thinkTimeMs = thinkTimeMs;
    this.task = task;
    this.observationBytes = 0;
    this.dead = false;
    this._base = null;
    this._citable = null;
    this._cache = new Map();
    this._sequence = 0;
  }

  kill() {
    this.dead = true;
  }

  _live() {
    if (this.dead) throw new AgentKilled();
  }

  async _call(method, args) {
    this._live();
    const result = await this.host.call(method, { ...this.identity.current(), ...args });
    this._live();
    return result;
  }

  /** Wait until the workspace is handed back to this agent under a newer generation. */
  async _awaitOwnership() {
    const stale = this.identity.current().ownerGeneration;
    const deadline = Date.now() + OWNERSHIP_WAIT_MS;
    while (this.identity.current().ownerGeneration <= stale) {
      if (Date.now() > deadline) throw new Error('ownership did not return to the agent');
      await new Promise((resolve) => setTimeout(resolve, OWNERSHIP_POLL_MS));
      this._live();
    }
    this._base = null;
  }

  async _observe(read) {
    let observed;
    try {
      observed = await this._call('observe', { view: 'outline', ...read, ...(this._base ? { sinceVersion: this._base } : {}) });
    } catch (error) {
      if (!OWNERSHIP_CODES.includes(error.code)) throw error;
      await this._awaitOwnership();
      return this._observe(read);
    }
    this.observationBytes += Buffer.byteLength(JSON.stringify(observed));
    const regions = {};
    for (const [selector, entry] of Object.entries(observed.regions)) {
      if (entry.missing) continue;
      if (entry.outline !== undefined) this._cache.set(selector, entry.outline);
      regions[selector] = this._cache.get(selector) ?? '';
    }
    this._base = observed.observationVersion;
    this._citable = observed.observationVersion;
    return { version: observed.observationVersion, url: observed.url, regions };
  }

  async view(kind) {
    const observed = await this._observe(VIEWS[kind]);
    return PARSERS[kind](observed.regions, observed.version);
  }

  /** Submit one action; refusals that call for a fresh decision become Redecide. */
  async _submit(spec) {
    let record;
    try {
      this._live();
      record = await this.host.submitAndWait({ ...this.identity.current(), ...spec });
      this._live();
    } catch (error) {
      if (error.code === 'agent-killed') throw error;
      if (OWNERSHIP_CODES.includes(error.code)) {
        await this._awaitOwnership();
        throw new Redecide(error.code);
      }
      if (REDECIDE_CODES.includes(error.code)) throw new Redecide(error.code);
      throw error;
    }
    if (record.postObservationVersion) this._citable = record.postObservationVersion;
    if (record.dispatchStatus === 'never-dispatched') {
      if (OWNERSHIP_CODES.includes(record.neverDispatched.code)) await this._awaitOwnership();
      throw new Redecide(record.neverDispatched.code);
    }
    if (record.outcome !== 'completed') throw new Error(`${spec.actionId}: ${record.outcome} (${record.outcomeBasis})`);
    return record;
  }

  _actionId(label) {
    this._sequence += 1;
    return `${this.task.id}-${this.checkpoint.get().incarnation ?? 0}-${this._sequence}-${label}`;
  }

  async navigate(url) {
    if (this._citable === null) await this._observe({ regions: ['h1'] });
    const spec = (version) => ({
      actionId: this._actionId('navigate'),
      authorizedIntent: this.task.intent,
      kind: 'navigate',
      target: { url },
      observationVersion: version,
      observationScope: { kind: 'regions', selectors: [] },
    });
    try {
      await this._submit(spec(this._citable));
    } catch (error) {
      if (!error.redecide) throw error;
      await this._observe({ regions: ['h1'] });
      await this._submit(spec(this._citable));
    }
  }

  async act(kind, target, { value, reference } = {}) {
    const scoped = (selectors) => ({ observationVersion: target.version, observationScope: { kind: 'regions', selectors } });
    const base = { authorizedIntent: this.task.intent, target: { handle: target.handle } };
    const ACTIONS = {
      add: () => ({
        ...base,
        actionId: this._actionId('add'),
        kind: 'click',
        ...scoped([target.region]),
        effect: { external: true, effectKey: `cart-add:${this.task.id}:${target.name}:${this._sequence}` },
        completion: { completedWhen: [{ kind: 'element-text-includes', selector: '#cart-summary', value: target.name }] },
      }),
      remove: () => ({
        ...base,
        actionId: this._actionId('remove'),
        kind: 'click',
        ...scoped([target.region]),
        effect: { external: true, effectKey: `cart-remove:${this.task.id}:${target.name}:${this._sequence}` },
        completion: { completedWhen: [{ kind: 'element-absent', selector: target.region }] },
      }),
      fill: () => ({ ...base, actionId: this._actionId('fill'), kind: 'fill', ...scoped(['#order-form']), params: { value } }),
      place: () => ({
        ...base,
        actionId: this._actionId('place'),
        kind: 'submit',
        ...scoped(['#order-form']),
        effect: {
          external: true,
          effectKey: `order:${this.task.id}`,
          ...(reference ? { externalIdempotencyKey: reference } : {}),
        },
        completion: { completedWhen: [{ kind: 'url-prefix', value: `${this.task.origin}/orders/` }] },
      }),
      download: () => ({ ...base, actionId: this._actionId('download'), kind: 'download', ...scoped(['#invoice']) }),
    };
    const record = await this._submit(ACTIONS[kind]());
    if (kind === 'place') return { orderId: orderIdFrom(record) };
    if (kind === 'download') return { sha256: record.evidence.effect.sha256 };
    return record;
  }

  /** The workspace kept dispatching while the agent was gone; its record says what happened. */
  async resolveInterruptedOrder() {
    const deadline = Date.now() + OUTCOME_WAIT_MS;
    for (;;) {
      const order = (await this._call('outstanding', {})).filter((entry) => entry.effectKey === `order:${this.task.id}`).at(-1);
      if (!order || order.dispatchStatus === 'never-dispatched') return { retry: true };
      if (order.outcome === 'completed') {
        await this._call('acknowledge', { actionId: order.actionId });
        return { orderId: orderIdFrom(order) };
      }
      if (order.outcome !== null) return { humanReview: true, reason: `order outcome ${order.outcome}` };
      if (Date.now() > deadline) return { humanReview: true, reason: 'order dispatch has not settled' };
      await new Promise((resolve) => setTimeout(resolve, OUTCOME_POLL_MS));
    }
  }
}

function orderIdFrom(record) {
  const observed = record.evidence?.conditions?.results?.[0]?.observed ?? '';
  return Number(/\/orders\/(\d+)/.exec(observed)?.[1]) || null;
}

export { FawnAgent };
