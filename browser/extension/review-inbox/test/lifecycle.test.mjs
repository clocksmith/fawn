// The life of an item, checked without a browser:
//   node --test test/lifecycle.test.mjs
//
// The first tests read the table in lifecycle.js. The rest load the real worker
// (background.js) against a stand-in for `chrome` and drive it the way the
// inbox page and the browser do: with messages and tab events.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { STATE, REMOVED, REASON, OUTCOME, isOpen, isTerminal, mayMove, refusal, whenTabCloses, whenDocumentIs } from '../lifecycle.js';

const { PENDING, APPROVED, FINISHING, BLOCKED, REFUSED, CONFIRMED, UNCONFIRMED } = STATE;
const TERMINAL = [BLOCKED, REFUSED, CONFIRMED, UNCONFIRMED];
// Written out again here on purpose: the test is a second opinion on the table, not a copy of it.
const ALLOWED = new Set([
  'pending>approved', 'pending>blocked', 'pending>removed',
  'approved>finishing', 'approved>blocked', 'approved>removed',
  'finishing>blocked', 'finishing>refused', 'finishing>confirmed', 'finishing>unconfirmed',
  'blocked>removed', 'refused>removed', 'confirmed>removed', 'unconfirmed>removed',
]);
const PAIRS = Object.values(STATE).flatMap(from => [...Object.values(STATE), REMOVED].map(to => [from, to]));

// As much of `chrome` as the worker touches. A tab is reduced to what an item holds on it: the id of its
// document and the map of reviewed elements, which injected functions see as the page's own. Nothing is
// ever under the click point, so a finish that gets as far as looking at the page ends as blocked.
function fakeChrome(tabs, items) {
  const stored = { items: structuredClone(items) }, listeners = {}, detached = [], injections = [];
  const event = name => ({ addListener: listener => { listeners[name] = listener; } });
  let gate = null;
  const chrome = {
    storage: { session: { get: async () => structuredClone(stored), set: async change => { Object.assign(stored, structuredClone(change)); } } },
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
      Object.assign(globalThis, { __fawnReviewed: tab.reviewed, document: { elementFromPoint: () => null } });
      try { return [{ frameId: 0, documentId: tab.documentId, result: func(...args) }]; }
      finally { delete globalThis.__fawnReviewed; delete globalThis.document; }
    } },
    debugger: { attach: async () => {}, sendCommand: async () => ({}), detach: async target => { detached.push(target.tabId); } },
  };
  return { chrome, stored, listeners, detached, injections,
    // Holds the next `tabs.get`, which is the first thing a finish does once it owns the item, until released.
    hold: () => { let open; gate = new Promise(resolve => { open = resolve; }); return () => { gate = null; open(); }; } };
}
const tab = (documentId, keys = []) => ({ documentId, reviewed: new Map(keys.map(key => [key, { isConnected: true, getBoundingClientRect: () => ({ x: 0, y: 0 }) }])) });
const item = (id, state, tabId = 7, more = {}) => ({ id, tabId, windowId: 1, documentId: 'document-1', url: 'http://localhost/', pageTitle: 'A page',
  rect: { x: 0, y: 0, width: 10, height: 10 }, point: { x: 5, y: 5 }, bound: 'key-' + id, image: 'AAAA', summary: { title: `Click "${id}"`, lines: [], recipe: null },
  state, createdAt: '2026-01-01T00:00:00.000Z', check: { at: '2026-01-01T00:00:00.000Z', holds: true, reason: REASON.matches },
  ...(TERMINAL.includes(state) ? { outcome: { text: 'An earlier result', detail: '', at: '2026-01-01T00:00:00.000Z' } } : {}), ...more });
const byId = list => Object.fromEntries(list.map(entry => [entry.id, entry]));
// Everything the stand-in does resolves without a timer, so one turn of the event loop lets the worker finish.
const quiet = () => new Promise(resolve => setImmediate(resolve));

let loads = 0;
async function worker(tabs, list) {
  const fake = fakeChrome(tabs, byId(list));
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
  // One item per pair. An item cannot be stored as `finishing` by a test (a new worker closes those as
  // interrupted), so those start approved and are moved there by the worker itself.
  const name = (from, to) => `${from}-${to}`;
  const { move, stored } = await worker({ 7: tab('document-1') }, PAIRS.map(([from, to]) => item(name(from, to), from === FINISHING ? APPROVED : from)));
  for (const [from, to] of PAIRS) if (from === FINISHING) await move(name(from, to), { state: FINISHING });
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
