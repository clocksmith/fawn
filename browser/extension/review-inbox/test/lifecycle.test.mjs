// The life of an item, checked without a browser:
//   node --test test/lifecycle.test.mjs
//
// The first tests read the table in lifecycle.js. The rest load the real worker
// (background.js) against a stand-in for `chrome` and drive it the way the
// inbox page and the browser do: with messages and tab events.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { STATE, TRANSITIONS, REMOVED, REASON, OUTCOME, SETTLE_MS, isOpen, isTerminal, mayMove, refusal, reconciled, whenTabCloses, whenDocumentIs } from '../lifecycle.js';

const { PENDING, APPROVED, FINISHING, UNKNOWN, RECONCILING, UNAPPLIED, BLOCKED, REFUSED, CONFIRMED } = STATE;
const TERMINAL = [BLOCKED, REFUSED, CONFIRMED];
// Written out again here on purpose: the test is a second opinion on the table, not a copy of it.
const ALLOWED = new Set([
  'pending>approved', 'pending>blocked', 'pending>removed',
  'approved>finishing', 'approved>blocked', 'approved>removed',
  'finishing>blocked', 'finishing>refused', 'finishing>confirmed', 'finishing>unknown',
  'unknown>reconciling', 'unknown>removed',
  'reconciling>confirmed', 'reconciling>unapplied', 'reconciling>unknown',
  'unapplied>approved', 'unapplied>removed',
  'blocked>removed', 'refused>removed', 'confirmed>removed',
]);
// States a test cannot store an item in, because a new worker takes them from the worker that owned them:
// each is reached from the state beside it by the worker itself.
const OWNED = { [FINISHING]: APPROVED, [RECONCILING]: UNKNOWN };
const PAIRS = Object.values(STATE).flatMap(from => [...Object.values(STATE), REMOVED].map(to => [from, to]));

// As much of `chrome` as the worker touches. A tab is reduced to what an item holds on it: the id of its
// document and the map of reviewed elements, which injected functions see as the page's own. Nothing is
// ever under the click point, so a finish that gets as far as looking at the page ends as blocked.
// `session` is what this browser session's storage holds: by default the mark a worker leaves there, so
// loading the worker is a worker restart. An empty one is a browser that was restarted.
function fakeChrome(tabs, items, session = { browserSession: 'earlier' }) {
  const stored = { items: structuredClone(items) }, sessionStored = structuredClone(session), listeners = {}, detached = [], injections = [], commands = [], fetches = [], network = new Set();
  const event = name => ({ addListener: listener => { listeners[name] = listener; } });
  let gate = null;
  const chrome = {
    storage: {
      local: { get: async () => structuredClone(stored), set: async change => { Object.assign(stored, structuredClone(change)); } },
      session: { get: async () => structuredClone(sessionStored), set: async change => { Object.assign(sessionStored, structuredClone(change)); } },
    },
    runtime: { onMessage: event('message') },
    tabs: {
      onRemoved: event('removed'), onUpdated: event('updated'),
      get: async id => { await gate; if (!tabs[id]) throw new Error(`No tab with id: ${id}.`); return { id, active: true, windowId: 1 }; },
      query: async () => [], update: async () => ({}),
      sendMessage: async () => ({ cancelled: true }),
    },
    windows: { update: async () => ({}) },
    scripting: { executeScript: async ({ target, func, args = [] }) => {
      const tab = tabs[target.tabId];
      if (!tab) throw new Error(`No tab with id: ${target.tabId}.`);
      injections.push(target.tabId);
      // The page as the injected functions see it: the reviewed elements, what is under the click point, and
      // the application's signs (a refusal message once the page shows one).
      const signs = selector => selector.includes('toastify') ? tab.toasts ?? [] : [];
      Object.assign(globalThis, { __fawnReviewed: tab.reviewed, requestAnimationFrame: callback => callback(),
        window: { scrollX: 0, scrollY: 0, scrollBy: () => {} },
        document: { elementFromPoint: () => tab.pointed ?? null, querySelectorAll: signs } });
      try { return [{ frameId: 0, documentId: tab.documentId, result: await func(...args) }]; }
      finally { for (const name of ['__fawnReviewed', 'document', 'window', 'requestAnimationFrame']) delete globalThis[name]; }
    } },
    debugger: {
      attach: async () => {}, detach: async target => { detached.push(target.tabId); },
      onEvent: { addListener: listener => network.add(listener), removeListener: listener => network.delete(listener) },
      sendCommand: async (target, method, params) => {
        commands.push(params?.type ? `${method}:${params.type}` : method);
        if (method === 'Page.captureScreenshot') return { data: tabs[target.tabId]?.image ?? 'AAAA' };
        // A trusted click that lands where the fence expects it, followed by what the tab's network does.
        if (method === 'Input.dispatchMouseEvent' && params.type === 'mouseReleased') {
          if (globalThis.__fawnFence) globalThis.__fawnFence.clicked = true;
          tabs[target.tabId]?.onClick?.(target.tabId, (name, body) => { for (const listener of network) listener({ tabId: target.tabId }, name, body); });
        }
        return {};
      },
    },
  };
  // The application's record, as a read-only GET returns it. `record` answers each read in turn.
  let record = () => { throw new Error('no record was expected'); };
  globalThis.fetch = async (url, options) => {
    fetches.push({ url: String(url), method: options?.method, body: options?.body });
    const answer = await record(String(url));
    return { ok: answer.status === undefined, status: answer.status ?? 200, json: async () => answer };
  };
  return { chrome, stored, sessionStored, listeners, detached, injections, commands, fetches, setRecord: answer => { record = answer; },
    // Holds the next `tabs.get`, which is the first thing a finish does once it owns the item, until released.
    hold: () => { let open; gate = new Promise(resolve => { open = resolve; }); return () => { gate = null; open(); }; } };
}
const tab = (documentId, keys = []) => ({ documentId, reviewed: new Map(keys.map(key => [key, { isConnected: true, getBoundingClientRect: () => ({ x: 0, y: 0 }) }])) });
const old = '2026-01-01T00:00:00.000Z';
const item = (id, state, tabId = 7, more = {}) => ({ id, tabId, windowId: 1, documentId: 'document-1', url: 'http://localhost/', pageTitle: 'A page',
  rect: { x: 0, y: 0, width: 10, height: 10 }, point: { x: 5, y: 5 }, bound: 'key-' + id, image: 'AAAA', summary: { title: `Click "${id}"`, lines: [], recipe: null },
  state, createdAt: '2026-01-01T00:00:00.000Z', check: { at: '2026-01-01T00:00:00.000Z', holds: true, reason: REASON.matches },
  ...(TERMINAL.includes(state) || state === UNKNOWN || state === UNAPPLIED ? { outcome: { text: 'An earlier result', detail: '', at: old } } : {}),
  ...(state === UNKNOWN || state === UNAPPLIED ? { finishingAt: old } : {}), ...more });
// A Forgejo merge item and the pull request record Forgejo's API returns for it.
const HEAD = 'a'.repeat(40), OTHER = 'b'.repeat(40);
const merge = (id, state, more = {}) => item(id, state, 7, { url: 'http://forgejo.test/ana/payables/pulls/12',
  summary: { title: 'Merge pull request #12 in ana/payables', lines: [], recipe: 'forgejo-merge',
    target: { base: 'http://forgejo.test', repo: 'ana/payables', number: 12, head: HEAD } }, ...more });
const pull = (more = {}) => ({ number: 12, state: 'open', merged: false, merge_commit_sha: null, head: { sha: HEAD },
  base: { repo: { full_name: 'ana/payables' } }, ...more });
const mergedPull = (head = HEAD) => pull({ state: 'closed', merged: true, merge_commit_sha: 'c'.repeat(40), merged_by: { login: 'ben' }, head: { sha: head } });
// A tab where every reviewed control is under its click point and looks as it did: one element held under
// each key, so whichever is asked about is the one at the point.
const live = (keys, more = {}) => {
  const element = { isConnected: true, getBoundingClientRect: () => ({ x: 0, y: 0 }) };
  return { documentId: 'document-1', reviewed: new Map(keys.map(key => [key, element])), pointed: element, ...more };
};
const byId = list => Object.fromEntries(list.map(entry => [entry.id, entry]));
// Everything the stand-in does resolves without a timer, so one turn of the event loop lets the worker finish.
const quiet = () => new Promise(resolve => setImmediate(resolve));

let loads = 0;
async function worker(tabs, list, session) {
  const fake = fakeChrome(tabs, byId(list), session);
  globalThis.chrome = fake.chrome;
  const { move } = await import(`../background.js?load=${++loads}`);
  await quiet();
  const send = message => new Promise(resolve => fake.listeners.message(message, {}, resolve));
  // A finish that has taken its item and is waiting on the tab: the item stays `finishing` until `go()`.
  const startFinish = async id => { const go = fake.hold(), reply = send({ inbox: 'finish', id }); await quiet(); return { go, reply }; };
  return { ...fake, move, send, startFinish };
}

test('the table allows exactly the listed moves and refuses every other one', () => {
  for (const [from, to] of PAIRS) {
    const allowed = ALLOWED.has(`${from}>${to}`);
    assert.equal(mayMove(from, to), allowed, `${from} -> ${to}`);
    assert.equal(refusal(from, to) === null, allowed, `${from} -> ${to} gives a reason exactly when refused`);
  }
  assert.equal(mayMove('nonsense', BLOCKED), false);
  assert.equal(refusal(FINISHING, REMOVED), REASON.removeWhileFinishing);
  assert.equal(refusal(PENDING, FINISHING), REASON.notApproved);
  assert.equal(refusal(APPROVED, APPROVED), REASON.notPending);
});

test('the moves the table refuses include every way from an unknown result to a click', () => {
  for (const from of [UNKNOWN, RECONCILING]) {
    assert.equal(mayMove(from, FINISHING), false, `${from} cannot be finished`);
    assert.equal(mayMove(from, APPROVED), false, `${from} cannot be approved`);
    assert.equal(mayMove(from, PENDING), false, `${from} cannot be picked again`);
  }
  assert.equal(refusal(UNKNOWN, FINISHING), REASON.unknownNoFinish);
  assert.equal(refusal(UNKNOWN, APPROVED), REASON.unknownNoApprove);
  assert.equal(refusal(RECONCILING, REMOVED), REASON.removeWhileReconciling);
  assert.equal(refusal(APPROVED, RECONCILING), REASON.notUnknown);
  // Reconciling ends only in what a read can say, and a not-applied item reaches a click only through approval.
  assert.deepEqual([...TRANSITIONS[RECONCILING]].sort(), [CONFIRMED, UNAPPLIED, UNKNOWN].sort());
  assert.equal(mayMove(UNAPPLIED, FINISHING), false);
});

test('a reconciliation concludes only what its reads support', () => {
  const words = { noun: 'merge', doneLabel: 'Merge confirmed' }, at = old;
  const view = (applied, signature = 's', changes = []) => ({ applied, changes, evidence: 'e', signature });
  assert.equal(reconciled({ unsupported: true }, words, at).state, UNKNOWN);
  assert.equal(reconciled({ failure: 'HTTP 502' }, words, at).outcome.detail, REASON.unreadable('HTTP 502'));
  // Done on any read is done, however soon.
  assert.equal(reconciled({ reads: [view(true)], sinceSentMs: 0 }, words, at).state, CONFIRMED);
  // Nothing found too soon after the click, or found differently on the two reads, is still unknown.
  assert.equal(reconciled({ reads: [view(false), view(false)], sinceSentMs: SETTLE_MS - 1 }, words, at).state, UNKNOWN);
  assert.equal(reconciled({ reads: [view(false, 'x'), view(false, 'y')], sinceSentMs: SETTLE_MS }, words, at).outcome.detail, REASON.readsDisagree);
  assert.equal(reconciled({ reads: [view(false)], sinceSentMs: SETTLE_MS }, words, at).state, UNKNOWN, 'one read is not enough');
  const settled = reconciled({ reads: [view(false), view(false)], sinceSentMs: SETTLE_MS }, words, at);
  assert.deepEqual([settled.state, settled.check.holds, settled.check.reason], [UNAPPLIED, null, REASON.termsHold]);
  const stale = reconciled({ reads: [view(false, 's', ['moved']), view(false, 's', ['moved'])], sinceSentMs: SETTLE_MS }, words, at);
  assert.deepEqual([stale.state, stale.check.holds, stale.check.reason], [UNAPPLIED, false, REASON.stale(['moved'])]);
});

test('terminal and open states are the ones the README names', () => {
  for (const state of Object.values(STATE)) {
    assert.equal(isTerminal(state), TERMINAL.includes(state), state);
    assert.equal(isOpen(state), state === PENDING || state === APPROVED, state);
  }
  const readme = readFileSync(new URL('../README.md', import.meta.url), 'utf8');
  for (const state of Object.values(STATE)) assert.match(readme, new RegExp('^\\| `' + state + '` \\|', 'm'), `the README's state table lists ${state}`);
});

test('a closed tab or a replaced document closes an open item as blocked and leaves every other item alone', () => {
  const at = '2026-01-02T00:00:00.000Z';
  for (const state of Object.values(STATE)) {
    const closed = whenTabCloses(item('a', state), at), replaced = whenDocumentIs(item('a', state), 'document-2', at);
    if (!isOpen(state)) { assert.equal(closed, null, state); assert.equal(replaced, null, state); continue; }
    assert.deepEqual(closed, { state: BLOCKED, check: { at, holds: false, reason: REASON.tabClosed }, outcome: { text: OUTCOME.blocked, detail: REASON.tabClosed, at } });
    assert.deepEqual(replaced, { state: BLOCKED, check: { at, holds: false, reason: REASON.documentReplaced }, outcome: { text: OUTCOME.blocked, detail: REASON.documentReplaced, at } });
    assert.equal(whenDocumentIs(item('a', state), 'document-1', at), null, 'the same document is not a change');
  }
});

test('the worker refuses every illegal move and leaves the item as it was', async () => {
  // One item per pair. An item cannot be stored as `finishing` or `reconciling` by a test (a new worker
  // takes those from the worker that owned them), so those start a step before and are moved there.
  const name = (from, to) => `${from}-${to}`;
  const { move, stored } = await worker({ 7: tab('document-1') }, PAIRS.map(([from, to]) => item(name(from, to), OWNED[from] ?? from)));
  for (const [from, to] of PAIRS) if (OWNED[from]) await move(name(from, to), { state: from });
  for (const [from, to] of PAIRS) {
    const id = name(from, to), before = structuredClone(stored.items[id]);
    assert.equal(before.state, from);
    if (ALLOWED.has(`${from}>${to}`)) {
      await move(id, { state: to });
      assert.equal(stored.items[id]?.state, to === REMOVED ? undefined : to, `${from} -> ${to}`);
    } else {
      await assert.rejects(move(id, { state: to }), { message: refusal(from, to) }, `${from} -> ${to}`);
      assert.deepEqual(stored.items[id], before, `${from} -> ${to} wrote nothing`);
    }
  }
  assert.equal(await move('never-picked', { state: BLOCKED }), null);
  assert.equal(stored.items['never-picked'], undefined, 'a move does not create an item');
});

test('removing an item that is being finished is refused, and the item is intact', async () => {
  const { send, stored, startFinish } = await worker({ 7: tab('document-1', ['key-a']) }, [item('a', APPROVED)]);
  const finish = await startFinish('a');
  const during = structuredClone(stored.items.a);
  assert.equal(during.state, FINISHING);
  assert.deepEqual(await send({ inbox: 'remove', id: 'a' }), { ok: false, error: REASON.removeWhileFinishing });
  assert.deepEqual(stored.items.a, during);
  assert.deepEqual((await send({ inbox: 'items' })).value, [during]);
  // The finish still ends the item itself, and then it can be cleared.
  finish.go();
  const reply = await finish.reply;
  assert.deepEqual([reply.ok, reply.value.state, reply.value.outcome.detail], [true, BLOCKED, REASON.covered]);
  assert.deepEqual(await send({ inbox: 'remove', id: 'a' }), { ok: true, value: null });
  assert.deepEqual(stored.items, {});
});

test('a result that arrives after its item has gone does not bring the item back', async () => {
  const { send, stored, startFinish } = await worker({ 7: tab('document-1', ['key-a']) }, [item('a', APPROVED), item('kept', PENDING)]);
  const finish = await startFinish('a');
  delete stored.items.a; // gone by some other hand, as `remove` used to allow
  finish.go();
  assert.deepEqual(await finish.reply, { ok: true, value: null });
  assert.deepEqual(Object.keys(stored.items), ['kept']);
  // The list the inbox draws is whole items only.
  const listed = (await send({ inbox: 'items' })).value;
  assert.deepEqual(listed.map(entry => [entry.id, typeof entry.createdAt, typeof entry.summary.title]), [['kept', 'string', 'string']]);
});

test('closing a tab closes its open items as blocked and nothing else', async () => {
  const terminal = TERMINAL.map(state => item('was-' + state, state));
  const tabs = { 7: tab('document-1'), 8: tab('document-1') };
  const { stored, listeners, startFinish, send } = await worker(tabs, [item('waiting', PENDING), item('ready', APPROVED), item('clicking', APPROVED), ...terminal, item('other', PENDING, 8)]);
  const finish = await startFinish('clicking');
  const before = structuredClone(stored.items);
  delete tabs[7];
  await listeners.removed(7, { windowId: 1, isWindowClosing: false });
  for (const id of ['waiting', 'ready']) {
    assert.equal(stored.items[id].state, BLOCKED, id);
    assert.deepEqual([stored.items[id].outcome.text, stored.items[id].outcome.detail], [OUTCOME.blocked, REASON.tabClosed], id);
    assert.deepEqual([stored.items[id].check.holds, stored.items[id].check.reason], [false, REASON.tabClosed], id);
  }
  for (const id of ['clicking', 'other', ...terminal.map(entry => entry.id)]) assert.deepEqual(stored.items[id], before[id], `${id} is left alone`);
  // The finish that was under way gives its own item its result.
  finish.go();
  const reply = await finish.reply;
  assert.deepEqual([reply.value.state, reply.value.outcome.detail], [BLOCKED, REASON.tabClosed]);
  assert.equal((await send({ inbox: 'approve', id: 'waiting' })).error, REASON.notPending, 'a closed item cannot be approved');
});

test('a replaced top document closes that tab\'s open items as blocked and nothing else', async () => {
  const terminal = TERMINAL.map(state => item('was-' + state, state));
  const tabs = { 7: tab('document-1'), 8: tab('document-1'), 9: tab('document-1') };
  const { stored, listeners, injections } = await worker(tabs, [item('waiting', PENDING), item('ready', APPROVED), ...terminal, item('other', PENDING, 8), item('done', CONFIRMED, 9)]);
  const before = structuredClone(stored.items);
  // A change that keeps the document (a new title, a script-made URL) closes nothing.
  await listeners.updated(7, { title: 'A new title' });
  assert.deepEqual(stored.items, before);
  // A tab with no open item is not asked anything.
  injections.length = 0;
  await listeners.updated(9, { status: 'complete' });
  assert.deepEqual(injections, []);
  tabs[7].documentId = 'document-2';
  await listeners.updated(7, { status: 'complete' });
  for (const id of ['waiting', 'ready']) {
    assert.equal(stored.items[id].state, BLOCKED, id);
    assert.deepEqual([stored.items[id].outcome.text, stored.items[id].outcome.detail], [OUTCOME.blocked, REASON.documentReplaced], id);
  }
  for (const id of ['other', 'done', ...terminal.map(entry => entry.id)]) assert.deepEqual(stored.items[id], before[id], `${id} is left alone`);
});

test('an item that ends or is removed is let go by its page, and the debugger leaves with the tab\'s last unfinished item', async () => {
  const tabs = { 7: tab('document-1', ['key-x', 'key-y', 'key-old']), 8: tab('document-1', ['key-z', 'key-w']) };
  const { send, listeners, detached, startFinish } = await worker(tabs,
    [item('x', PENDING), item('y', APPROVED), item('old', REFUSED), item('z', PENDING, 8), item('w', APPROVED, 8)]);
  const keys = id => [...tabs[id].reviewed.keys()];

  // Removed while another item on the tab is still open: the page lets go of this one, the debugger stays.
  await send({ inbox: 'remove', id: 'x' }); await quiet();
  assert.deepEqual(keys(7), ['key-y', 'key-old']);
  assert.deepEqual(detached, []);
  // Clearing an item that had already ended does not take the debugger from the open one either.
  await send({ inbox: 'remove', id: 'old' }); await quiet();
  assert.deepEqual(keys(7), ['key-y']);
  assert.deepEqual(detached, []);
  // The last one removed: now the tab is let go.
  await send({ inbox: 'remove', id: 'y' }); await quiet();
  assert.deepEqual(keys(7), []);
  assert.deepEqual(detached, [7]);

  // Reaching a terminal state is the same teardown. While one item is being finished, the tab is kept.
  const finish = await startFinish('w');
  tabs[8].documentId = 'document-2';
  await listeners.updated(8, { status: 'complete' }); await quiet();
  assert.deepEqual(keys(8), ['key-w'], 'the blocked item is let go; the one being finished is still held');
  assert.deepEqual(detached, [7]);
  finish.go();
  assert.deepEqual([(await finish.reply).value.state, (await finish.reply).value.outcome.detail], [BLOCKED, REASON.documentReplaced]);
  await quiet();
  assert.deepEqual(keys(8), []);
  assert.deepEqual(detached, [7, 8]);
});

test('a cancelled pick makes no item and lets the debugger go, unless the tab still has an open item', async () => {
  const { send, stored, detached } = await worker({ 7: tab('document-1'), 8: tab('document-1') }, [item('held', PENDING, 8)]);
  const replies = await Promise.all([send({ inbox: 'pick', tabId: 7 }), send({ inbox: 'pick', tabId: 8 })]);
  await quiet();
  assert.deepEqual(replies, [{ ok: true, value: null }, { ok: true, value: null }]);
  assert.deepEqual(Object.keys(stored.items), ['held']);
  assert.deepEqual(detached, [7]);
});

test('an unknown result blocks Finish, approval and every retry, and the item and the page are left as they were', async () => {
  const { send, stored, commands, injections, fetches } = await worker({ 7: live(['key-u']) }, [merge('u', UNKNOWN)]);
  const before = structuredClone(stored.items.u);
  injections.length = 0;
  for (let i = 0; i < 3; i++) assert.deepEqual(await send({ inbox: 'finish', id: 'u' }), { ok: false, error: REASON.unknownNoFinish });
  assert.deepEqual(await send({ inbox: 'approve', id: 'u' }), { ok: false, error: REASON.unknownNoApprove });
  // Check again is not offered for it and changes nothing.
  assert.deepEqual((await send({ inbox: 'check', id: 'u' })).value, before);
  await quiet();
  assert.deepEqual(stored.items.u, before);
  assert.deepEqual([commands, injections, fetches], [[], [], []], 'nothing was sent to the page or the application');
});

test('a reconciliation reads the record with GETs only and never clicks, runs in the page or writes', async () => {
  const cases = [
    { id: 'merged', answers: [mergedPull()], state: CONFIRMED, detail: /merged by ben as commit cccccccccc from head aaaaaaaaaa/ },
    { id: 'open', answers: [pull(), pull()], state: UNAPPLIED, detail: /not merged, at head aaaaaaaaaa/, holds: null },
    { id: 'moved', answers: [pull({ head: { sha: OTHER } }), pull({ head: { sha: OTHER } })], state: UNAPPLIED, holds: false, reason: /Stale approval: pull request #12 now has head commit bbbbbbbbbb; the reviewed one was aaaaaaaaaa/ },
    { id: 'someone-else', answers: [mergedPull(OTHER), mergedPull(OTHER)], state: UNAPPLIED, holds: false, reason: /already merged/ },
    { id: 'soon', answers: [pull(), pull()], more: { finishingAt: new Date().toISOString() }, state: UNKNOWN, detail: /may still be in progress/ },
    { id: 'disagree', answers: [pull(), pull({ head: { sha: OTHER } })], state: UNKNOWN, detail: new RegExp(REASON.readsDisagree.replace(/\./g, '\\.')) },
    { id: 'down', answers: [{ status: 502 }], state: UNKNOWN, detail: /answered HTTP 502/ },
    { id: 'elsewhere', answers: [], more: { summary: { title: 'x', lines: [], recipe: 'forgejo-merge', target: { base: 'http://elsewhere.test', repo: 'ana/payables', number: 12, head: HEAD } } }, state: UNKNOWN, detail: /not the reviewed page's origin/ },
    { id: 'plain', answers: [], more: { summary: { title: 'Click "Pay"', lines: [], recipe: null } }, state: UNKNOWN, detail: new RegExp(REASON.noRecord.slice(0, 40)) },
  ];
  const { send, stored, commands, injections, fetches, setRecord } = await worker({ 7: live(cases.map(c => 'key-' + c.id)) },
    cases.map(c => merge(c.id, UNKNOWN, c.more)));
  for (const c of cases) {
    const answers = [...c.answers];
    setRecord(() => answers.shift() ?? (() => { throw new Error('one read too many'); })());
    injections.length = 0; commands.length = 0; const seen = fetches.length;
    const reply = await send({ inbox: 'reconcile', id: c.id });
    assert.equal(reply.ok, true, `${c.id}: ${reply.error}`);
    const after = stored.items[c.id];
    assert.equal(after.state, c.state, c.id);
    if (c.detail) assert.match(after.outcome.detail, c.detail, c.id);
    if ('holds' in c) assert.equal(after.check.holds, c.holds, c.id);
    if (c.reason) assert.match(after.check.reason, c.reason, c.id);
    assert.equal(answers.length, 0, `${c.id} read the record as many times as expected`);
    assert.deepEqual(commands, [], `${c.id}: no debugger command, so no click`);
    assert.deepEqual(injections.filter(tabId => tabId === 7 && c.state !== CONFIRMED), [], `${c.id}: nothing ran in the page`);
    for (const call of fetches.slice(seen)) {
      assert.deepEqual([call.method, call.body], ['GET', undefined], `${c.id}: a read only`);
      assert.equal(call.url, 'http://forgejo.test/api/v1/repos/ana/payables/pulls/12');
    }
  }
  // A second reconcile of an item that is no longer unknown is refused; so is one of an item never picked.
  assert.deepEqual(await send({ inbox: 'reconcile', id: 'merged' }), { ok: false, error: REASON.notUnknown });
  assert.deepEqual(await send({ inbox: 'reconcile', id: 'never' }), { ok: false, error: REASON.notUnknown });
});

test('not applied goes back to approved only by an explicit approval that passes every check again', async () => {
  const tabs = { 7: live(['key-n', 'key-s']) };
  const { send, stored, commands, setRecord } = await worker(tabs, [merge('n', UNKNOWN), merge('s', UNKNOWN, { bound: 'key-s' })]);
  setRecord(() => pull());
  await send({ inbox: 'reconcile', id: 'n' });
  assert.equal(stored.items.n.state, UNAPPLIED);
  // Nothing moves it on by itself, and Finish is refused.
  await new Promise(resolve => setTimeout(resolve, 50));
  assert.equal(stored.items.n.state, UNAPPLIED);
  assert.deepEqual(await send({ inbox: 'finish', id: 'n' }), { ok: false, error: REASON.notApproved });
  assert.equal(commands.includes('Input.dispatchMouseEvent:mousePressed'), false);
  // Approve runs the checks of a first approval: here the pull request moved in the meantime.
  setRecord(() => pull({ head: { sha: OTHER } }));
  const refused = await send({ inbox: 'approve', id: 'n' });
  assert.equal(refused.ok, false);
  assert.match(refused.error, /Stale approval: .*head commit bbbbbbbbbb.* Review it again before approving\./);
  assert.equal(stored.items.n.state, UNAPPLIED);
  // Back as it was: the person approves exactly this again.
  setRecord(() => pull());
  const approved = await send({ inbox: 'approve', id: 'n' });
  assert.equal(approved.ok, true, approved.error);
  assert.deepEqual([approved.value.state, approved.value.outcome, approved.value.earlier.map(entry => entry.text)],
    [APPROVED, null, ['An earlier result', OUTCOME.notApplied('merge')]]);
});

test('Finish blocks a stale approval before anything is clicked, and says what changed', async () => {
  const { send, commands, setRecord } = await worker({ 7: live(['key-a', 'key-b', 'key-c']) },
    [merge('a', APPROVED), merge('b', APPROVED, { bound: 'key-b' }), merge('c', APPROVED, { bound: 'key-c' })]);
  const finished = async (id, answer) => { setRecord(answer); commands.length = 0; const reply = await send({ inbox: 'finish', id }); return [reply.value.state, reply.value.outcome.detail]; };
  const [state, detail] = await finished('a', () => pull({ head: { sha: OTHER } }));
  assert.equal(state, BLOCKED);
  assert.equal(detail, REASON.stale(['pull request #12 now has head commit bbbbbbbbbb; the reviewed one was aaaaaaaaaa']));
  assert.equal(commands.includes('Input.dispatchMouseEvent:mousePressed'), false, 'no click');
  assert.deepEqual(await finished('b', () => mergedPull()), [BLOCKED, REASON.stale(['pull request #12 is already merged'])]);
  // A record that cannot be read cannot vouch for the approval.
  assert.deepEqual(await finished('c', () => ({ status: 404 })), [BLOCKED, REASON.recordUnread('/api/v1/repos/ana/payables/pulls/12 answered HTTP 404')]);
  assert.equal(commands.includes('Input.dispatchMouseEvent:mousePressed'), false);
});

test('a write whose answer is lost or is a gateway error is unknown even when the page shows an error; a real refusal is refused', async () => {
  // Each tab's click sends one POST and then the page shows its error message. What differs is the answer.
  const clicking = answer => (tabId, emit) => {
    emit('Network.requestWillBeSent', { requestId: 'r' + tabId, request: { method: 'POST', url: 'http://forgejo.test/ana/payables/pulls/12/merge' } });
    if (answer.failed) emit('Network.loadingFailed', { requestId: 'r' + tabId, errorText: answer.failed });
    else emit('Network.responseReceived', { requestId: 'r' + tabId, response: { status: answer.status } });
    tabs[tabId].toasts = [{ textContent: 'An error', getClientRects: () => [1] }];
  };
  const tabs = {
    7: live(['key-reset'], { onClick: clicking({ failed: 'net::ERR_CONNECTION_RESET' }) }),
    8: live(['key-gateway'], { onClick: clicking({ status: 502 }) }),
    9: live(['key-refused'], { onClick: clicking({ status: 400 }) }),
  };
  const on = (id, tabId) => merge(id, APPROVED, { tabId, bound: 'key-' + id });
  const { send, setRecord } = await worker(tabs, [on('reset', 7), on('gateway', 8), on('refused', 9)]);
  setRecord(() => pull());
  const reset = (await send({ inbox: 'finish', id: 'reset' })).value;
  assert.deepEqual([reset.state, reset.outcome.text], [UNKNOWN, OUTCOME.notConfirmed('merge')]);
  assert.match(reset.outcome.detail, /POST \/ana\/payables\/pulls\/12\/merge: net::ERR_CONNECTION_RESET/);
  const gateway = (await send({ inbox: 'finish', id: 'gateway' })).value;
  assert.equal(gateway.state, UNKNOWN);
  assert.match(gateway.outcome.detail, /HTTP 502/);
  const refused = (await send({ inbox: 'finish', id: 'refused' })).value;
  assert.deepEqual([refused.state, refused.outcome.text, refused.outcome.detail], [REFUSED, OUTCOME.refused, 'An error']);
  // The unknown ones are not finished again.
  assert.deepEqual(await send({ inbox: 'finish', id: 'reset' }), { ok: false, error: REASON.unknownNoFinish });
});

test('a worker that stops while reconciling leaves the item unknown, having written nothing', async () => {
  const { stored, fetches } = await worker({ 7: tab('document-1') }, [merge('r', RECONCILING, { reconcilingBy: 'an-earlier-worker' })]);
  assert.deepEqual([stored.items.r.state, stored.items.r.outcome.detail], [UNKNOWN, REASON.reconcileStopped]);
  assert.deepEqual(fetches, []);
});

test('after a browser restart, an item whose click may have gone out is unknown and nothing reviewed can be clicked', async () => {
  // Items are kept where a browser restart does not wipe them. The tabs and documents they were bound to are gone;
  // tab 7 now belongs to some other page, which holds none of the reviewed elements.
  const list = [item('waiting', PENDING), item('ready', APPROVED), merge('clicking', FINISHING, { finishingBy: 'a-worker-before-the-restart' }),
    merge('reading', RECONCILING, { reconcilingBy: 'a-worker-before-the-restart' }), merge('unknown', UNKNOWN), merge('unapplied', UNAPPLIED), item('done', CONFIRMED)];
  const { stored, sessionStored, send, setRecord } = await worker({ 7: live(['key-other']) }, list, {});
  const state = id => [stored.items[id].state, stored.items[id].outcome?.detail, stored.items[id].tabId];
  assert.deepEqual(state('waiting'), [BLOCKED, REASON.browserRestarted, null]);
  assert.deepEqual(state('ready'), [BLOCKED, REASON.browserRestarted, null]);
  assert.deepEqual(state('clicking'), [UNKNOWN, REASON.browserStopped, null]);
  assert.deepEqual(state('reading'), [UNKNOWN, REASON.reconcileStopped, null]);
  assert.deepEqual([stored.items.unknown.state, stored.items.unknown.outcome.text, stored.items.unknown.tabId], [UNKNOWN, 'An earlier result', null], 'kept as it was, without its tab');
  assert.equal(stored.items.done.state, CONFIRMED);
  assert.ok(sessionStored.browserSession, 'the new session is marked, so a worker restart is not taken for a browser restart');
  // Nothing of it can be finished; the unknown one is reconciled from Forgejo's record as before.
  for (const id of ['ready', 'clicking', 'unknown']) assert.equal((await send({ inbox: 'finish', id })).ok, false, id);
  const again = await send({ inbox: 'approve', id: 'unapplied' });
  assert.deepEqual([again.ok, stored.items.unapplied.state], [false, UNAPPLIED]);
  assert.match(again.error, new RegExp(REASON.tabClosed));
  setRecord(() => mergedPull());
  assert.equal((await send({ inbox: 'reconcile', id: 'clicking' })).value.state, CONFIRMED);
});
