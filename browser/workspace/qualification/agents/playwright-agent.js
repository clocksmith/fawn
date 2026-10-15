// Fawn workspace differentiation — control tools: the agent over Playwright agent tooling.
//
// Each view takes a page-wide agent snapshot; the agent pays for the incremental text when there
// is one, and parses the full snapshot it is given alongside. Actions target aria refs from the
// latest snapshot. Consequential calls are checkpointed before and after, every call waits while
// a human holds control, and an interrupted order is resolved from the application's order list,
// its idempotency reference, or the current page.

const ITEM = /^\s*- text: (.+?) \$(\d+)\.(\d{2})(?: (\d+) in stock| (out of stock))?$/;
const BUTTON = /^\s*- button "([^"]+)" (?:\[[^\]]+\] )*\[ref=(e\d+)\]/;
const TEXTBOX = /^\s*- textbox "([^"]+)" (?:\[[^\]]+\] )*\[ref=(e\d+)\](?:: (.*))?$/;
const LINK = /^\s*- link "([^"]+)" \[ref=(e\d+)\]/;
const HEADING = /^\s*- heading "([^"]+)"/;
const ORDER_ROW = /Order (\d+): (.+?) to (.+?); reference (.+?)"?$/;

class AgentKilled extends Error {
  constructor() {
    super('agent process ended');
    this.code = 'agent-killed';
  }
}

/** Pairs each "- text: Name $p ..." line with the button that follows it. */
function itemsWithButtons(lines, buttonName) {
  const found = [];
  lines.forEach((line, index) => {
    const item = ITEM.exec(line);
    if (!item) return;
    const button = BUTTON.exec(lines[index + 1] ?? '');
    if (!button || button[1] !== buttonName) return;
    found.push({
      name: item[1],
      price: Number(item[2]) * 100 + Number(item[3]),
      stock: item[5] ? 0 : Number(item[4] ?? 0),
      ref: button[2],
    });
  });
  return found;
}

const PARSERS = Object.freeze({
  catalog: (lines) => ({ items: itemsWithButtons(lines, 'Add to cart') }),
  cart: (lines) => ({ lines: itemsWithButtons(lines, 'Remove') }),
  checkout: (lines) => {
    const field = (name) => {
      const line = lines.map((candidate) => TEXTBOX.exec(candidate)).find((match) => match?.[1] === name);
      return line ? { ref: line[2], value: line[3] ?? '' } : null;
    };
    const place = lines.map((line) => BUTTON.exec(line)).find((match) => match?.[1] === 'Place order');
    return { address: field('Address'), reference: field('Reference'), place: place ? { ref: place[2] } : null };
  },
  confirmation: (lines) => {
    const heading = lines.map((line) => HEADING.exec(line)).find(Boolean)?.[1] ?? '';
    const link = lines.map((line) => LINK.exec(line)).find((match) => match?.[1] === 'Download invoice');
    return { orderId: Number(/Order (\d+) placed/.exec(heading)?.[1]) || null, invoice: link ? { ref: link[2] } : null, heading, lines };
  },
  orders: (lines) => ({
    found: lines.some((line) => HEADING.exec(line)?.[1] === 'Orders'),
    orders: lines.map((line) => ORDER_ROW.exec(line)).filter(Boolean)
      .map(([, id, items, address, reference]) => ({ id: Number(id), items, address, reference })),
  }),
});

class PlaywrightAgent {
  constructor({ host, pause, checkpoint, thinkTimeMs, task }) {
    this.host = host;
    this.pause = pause;
    this.checkpoint = checkpoint;
    this.thinkTimeMs = thinkTimeMs;
    this.task = task;
    this.observationBytes = 0;
    this.dead = false;
  }

  kill() {
    this.dead = true;
  }

  _live() {
    if (this.dead) throw new AgentKilled();
  }

  async _tool(op, args = {}, { consequential = false } = {}) {
    this._live();
    await this.pause.whileHeld();
    this._live();
    if (consequential) this.checkpoint.update({ lastCall: { op, args, phase: 'calling' } });
    const result = await this.host.request(op, args);
    this._live();
    if (consequential) this.checkpoint.update({ lastCall: { op, args, phase: 'done' } });
    return result;
  }

  async _snapshot() {
    const snapshot = await this._tool('snapshot');
    this.observationBytes += Buffer.byteLength(JSON.stringify({ url: snapshot.url, snapshot: snapshot.incremental ?? snapshot.full }));
    return snapshot;
  }

  async view(kind) {
    const snapshot = await this._snapshot();
    return PARSERS[kind](snapshot.full.split('\n'));
  }

  _requireAllowed(url) {
    if (new URL(url).origin !== this.task.origin) throw new Error(`origin of ${url} is not allowed`);
  }

  async navigate(url) {
    this._requireAllowed(url);
    await this._tool('navigate', { url });
  }

  async act(kind, target, { value } = {}) {
    if (kind === 'fill') return this._tool('fill', { ref: target.ref, value }, { consequential: true });
    if (kind === 'download') return this._tool('download', { ref: target.ref }, { consequential: true });
    if (kind === 'place') {
      this._requireAllowed((await this._tool('formAction')).action);
      const placed = await this._tool('click', { ref: target.ref }, { consequential: true });
      return { orderId: Number(/\/orders\/(\d+)/.exec(placed.url)?.[1]) || null };
    }
    return this._tool('click', { ref: target.ref }, { consequential: true });
  }

  /**
   * The order call began but its result was lost. Check the page the browser is on, then the
   * application's order list; with an idempotency reference a resubmission is safe; otherwise
   * the outcome is unknown and goes to human review.
   */
  async resolveInterruptedOrder(task, progress) {
    const current = PARSERS.confirmation((await this._snapshot()).full.split('\n'));
    if (current.orderId && current.lines.some((line) => line.includes(`Ship to: ${progress.address};`))) {
      return { orderId: current.orderId };
    }
    await this.navigate(`${task.origin}/orders`);
    const list = await this.view('orders');
    if (list.found) {
      const match = list.orders.find((order) => order.address === progress.address
        && order.reference === (progress.reference ?? 'none'));
      return match ? { orderId: match.id } : { retry: true };
    }
    if (progress.reference) return { retry: true };
    return { humanReview: true, reason: 'order outcome unknown: no confirmation on the page, no order list, no idempotency' };
  }
}

export { PlaywrightAgent };
