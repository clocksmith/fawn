// The click fence in page.js, checked without a browser:
//   node --test test/page.test.mjs
//
// page.js is loaded as the content script it is, against a stand-in for the
// window it listens on. Elements are reduced to a tree; an event carries its
// target and its composed path, as a trusted event in a page does.
import assert from 'node:assert/strict';
import { test } from 'node:test';

const listeners = {};
globalThis.window = { addEventListener: (type, listener) => { (listeners[type] ??= []).push(listener); } };
window.top = window;
globalThis.chrome = { runtime: { onMessage: { addListener: () => {} } } };
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
  globalThis.__fawnFence = { element, clicked: false, blocked: false };
  const stopped = press(target), fence = globalThis.__fawnFence;
  globalThis.__fawnFence = null;
  return { clicked: fence.clicked, blocked: fence.blocked, stopped };
};

test('a click on the reviewed control or anything inside it passes the fence', () => {
  for (const target of [button, label, icon, path]) assert.deepEqual(fenced(button, target), { clicked: true, blocked: false, stopped: 0 }, target.name);
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
  assert.deepEqual(fenced(inner, innerLabel), { clicked: true, blocked: false, stopped: 0 });
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
