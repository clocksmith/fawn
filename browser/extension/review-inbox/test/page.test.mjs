// The click fence in page.js, checked without a browser:
//   node --test test/page.test.mjs
//
// page.js is loaded as the content script it is, against a stand-in for the
// window it listens on. Elements are reduced to a tree; an event carries its
// target and its composed path, as a trusted event in a page does.
import assert from 'node:assert/strict';
import { test } from 'node:test';

const listeners = {};
let onMessage = null;
globalThis.window = { innerWidth: 1000, innerHeight: 700,
  addEventListener: (type, listener) => { (listeners[type] ??= []).push(listener); },
  removeEventListener: (type, listener) => { listeners[type] = (listeners[type] ?? []).filter(other => other !== listener); } };
window.top = window;
globalThis.chrome = { runtime: { onMessage: { addListener: listener => { onMessage = listener; } } } };
// Enough of a document for the picker's outline and hint, which are drawn and removed.
globalThis.document = { title: 'Payments', querySelector: () => null, documentElement: { append: () => {} },
  createElement: () => ({ style: {}, textContent: '', remove: () => {} }) };
globalThis.location = { href: 'http://shop.test/pay' };
globalThis.requestAnimationFrame = callback => callback();
await import('../page.js');

// A node in a tree: `contains` and the path an event takes from it, through shadow roots to their hosts.
const node = (name, parent = null, more = {}) => {
  const element = { name, parentNode: parent, ...more };
  element.contains = other => { for (let at = other; at; at = at.parentNode ?? at.host) if (at === element) return true; return false; };
  return element;
};
const pathOf = target => { const path = []; for (let at = target; at; at = at.parentNode ?? at.host) path.push(at); return path; };
// The window sees what a listener on it sees: a target retargeted out of shadow roots, and the full composed path.
const retarget = target => { let at = target; for (let up = target; up; up = up.parentNode ?? up.host) if (up.isShadowRoot) at = up.host; return at; };
function press(target, types = ['pointerdown', 'mousedown', 'pointerup', 'mouseup', 'click']) {
  let stopped = 0;
  for (const type of types) {
    const event = { type, isTrusted: true, target: retarget(target), composedPath: () => pathOf(target),
      stopImmediatePropagation: () => { stopped++; }, preventDefault: () => {} };
    for (const listener of listeners[type] ?? []) listener(event);
  }
  return stopped;
}

const body = node('body'), form = node('form', body);
const button = node('button', form), label = node('span', button), icon = node('svg', label), path = node('path', icon);
const twin = node('button (a look-alike sibling)', form), twinLabel = node('span', twin);
const host = node('custom-button', form), root = node('#shadow-root', null, { isShadowRoot: true, host }), inner = node('button', root), innerLabel = node('span', inner);

const fenced = (element, target) => {
  globalThis.__fawnFence = { element, clicked: false, blocked: false, down: false };
  const stopped = press(target), fence = globalThis.__fawnFence;
  globalThis.__fawnFence = null;
  return { clicked: fence.clicked, blocked: fence.blocked, down: fence.down, stopped };
};

test('a click on the reviewed control or anything inside it passes the fence', () => {
  for (const target of [button, label, icon, path]) assert.deepEqual(fenced(button, target), { clicked: true, blocked: false, down: true, stopped: 0 }, target.name);
});

test('the fence records a press that reached the control even when no click follows', () => {
  globalThis.__fawnFence = { element: button, clicked: false, blocked: false, down: false };
  press(label, ['pointerdown', 'mousedown']);
  assert.deepEqual(globalThis.__fawnFence, { element: button, clicked: false, blocked: false, down: true });
  globalThis.__fawnFence = { element: button, clicked: false, blocked: false, down: false };
  press(twin, ['pointerdown', 'mousedown']);
  assert.deepEqual([globalThis.__fawnFence.down, globalThis.__fawnFence.blocked], [false, true], 'a press on a look-alike is stopped and is not a press');
  globalThis.__fawnFence = null;
});

test('a click on a look-alike sibling, on what holds the control, or on anything else is stopped', () => {
  for (const target of [twin, twinLabel, form, body]) {
    const seen = fenced(button, target);
    assert.equal(seen.clicked, false, target.name);
    assert.equal(seen.blocked, true, target.name);
    assert.equal(seen.stopped, 5, `${target.name}: every event of the click is stopped`);
  }
});

test('inside an open shadow root the reviewed control is found on the composed path, not by the retargeted host', () => {
  assert.deepEqual(fenced(inner, innerLabel), { clicked: true, blocked: false, down: true, stopped: 0 });
  // The host is not the reviewed control, and neither is another control beside it.
  assert.equal(fenced(inner, host).blocked, true);
  assert.equal(fenced(inner, twin).blocked, true);
});

test('with no fence armed, or for an event the page made itself, the fence does nothing', () => {
  globalThis.__fawnFence = null;
  assert.equal(press(twin), 0);
  globalThis.__fawnFence = { element: button, clicked: false, blocked: false };
  for (const listener of listeners.click) listener({ type: 'click', isTrusted: false, target: twin, composedPath: () => pathOf(twin), stopImmediatePropagation: () => assert.fail('stopped') });
  assert.deepEqual(globalThis.__fawnFence, { element: button, clicked: false, blocked: false });
  globalThis.__fawnFence = null;
});

// The picker: a plain click chooses the control alone; Shift and a click, then a drag, add an area around it
// that is compared too.
const payButton = { innerText: 'Pay 1,280.00', type: 'button', form: null, getAttribute: () => null, closest: () => payButton,
  getBoundingClientRect: () => ({ x: 100, y: 200, left: 100, top: 200, right: 180, bottom: 230, width: 80, height: 30 }) };
const event = (type, more = {}) => ({ type, isTrusted: true, target: payButton, composedPath: () => [payButton], clientX: 0, clientY: 0,
  stopImmediatePropagation: () => {}, preventDefault: () => {}, ...more });
const send = (type, more) => { for (const listener of [...(listeners[type] ?? [])]) listener(event(type, more)); };
const picking = () => new Promise(resolve => onMessage({ fawn: 'pick' }, {}, resolve));
const control = { x: 100, y: 200, width: 80, height: 30 }, centre = { x: 140, y: 215 };

test('a plain click chooses the control and compares only its own pixels', async () => {
  const picked = picking();
  send('click', { clientX: 140, clientY: 215 });
  const result = await picked;
  assert.deepEqual([result.rect, result.point, result.area ?? null], [control, centre, null]);
});

test('Shift and a click, then a drag, add a person-chosen area around the control, never less than the control', async () => {
  const drag = async (from, to) => {
    const picked = picking();
    send('click', { clientX: 140, clientY: 215, shiftKey: true });
    send('mousedown', { clientX: from.x, clientY: from.y });
    send('mousemove', { clientX: to.x, clientY: to.y });
    // A drag that ends on another element, or outside the window, fires no click: the mouseup ends it.
    send('mouseup', { clientX: to.x, clientY: to.y });
    return Promise.race([picked, new Promise((_, reject) => setTimeout(() => reject(new Error('the pick never ended')), 1000))]);
  };
  const around = await drag({ x: 60, y: 150 }, { x: 400, y: 260 });
  assert.deepEqual([around.rect, around.point, around.area], [control, centre, { x: 60, y: 150, width: 340, height: 110 }]);
  assert.match(around.summary.lines.at(-1), /^Also compared: the area you drew around the button, 340 × 110 px$/);
  // A box that misses part of the control still covers all of it; one past the edge of the window stops there.
  assert.deepEqual((await drag({ x: 150, y: 210 }, { x: 500, y: 400 })).area, { x: 100, y: 200, width: 400, height: 200 });
  assert.deepEqual((await drag({ x: 900, y: 600 }, { x: 1300, y: 900 })).area, { x: 100, y: 200, width: 900, height: 500 });
  // The click the browser makes from that same mouseup comes in the same task, right after it: it is
  // swallowed, so the page never sees it. A click the person makes a moment later is theirs and reaches the page.
  const clickReaches = () => { let reached = true;
    const click = event('click', { clientX: 400, clientY: 260, stopImmediatePropagation: () => { reached = false; } });
    for (const listener of [...(listeners.click ?? [])]) listener(click);
    return reached; };
  const picked = picking();
  send('click', { clientX: 140, clientY: 215, shiftKey: true });
  send('mousedown', { clientX: 60, clientY: 150 });
  send('mouseup', { clientX: 400, clientY: 260 });
  assert.equal(clickReaches(), false, 'the click made from the mouseup is stopped');
  await picked;
  const quietDrag = drag({ x: 60, y: 150 }, { x: 400, y: 260 });
  await quietDrag;
  await new Promise(resolve => setTimeout(resolve, 200));
  assert.equal(clickReaches(), true, 'a deliberate click 200 ms after a drag that made no click is not swallowed');
  // Esc after Shift and a click keeps the control alone.
  const plain = picking();
  send('click', { clientX: 140, clientY: 215, shiftKey: true });
  send('keydown', { key: 'Escape' });
  assert.equal((await plain).area ?? null, null);
});
