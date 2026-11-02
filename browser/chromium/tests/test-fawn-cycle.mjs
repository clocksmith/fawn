// Focused CPU contracts: visible-time lerps, one-shot handoffs, and pointer consumption.
import assert from 'node:assert/strict';
import { createCycleClock, smootherstep } from '../resources/fawn-cycle-clock.js';
import { packSceneWeights } from '../resources/fawn-scenes.js';
import { createCycleInteraction, MAGNETIC_RELEASE } from '../resources/fawn-cycle-interaction.js';

const near = (actual, expected) => assert.ok(Math.abs(actual - expected) < 1e-10,
  `${actual} != ${expected}`);
const clock = createCycleClock();
for (const [time, effect, next, blend] of [
  [0, 'flow', 'wave', 0], [8, 'flow', 'wave', 0], [9.5, 'flow', 'wave', 0.103515625],
  [11, 'flow', 'wave', 0.5], [14, 'wave', 'particles', 0],
  [22, 'wave', 'particles', 0], [24, 'wave', 'particles', 0.5],
  [26, 'particles', 'flow', 0], [34, 'particles', 'flow', 0],
  [36, 'particles', 'flow', 0.5], [38, 'flow', 'wave', 0],
]) {
  const state = clock.advance(time - clock.state().elapsedS);
  assert.equal(state.effect, effect); assert.equal(state.next, next);
  near(state.blend, blend);
  near(Object.values(state.weights).reduce((a, b) => a + b), 1);
  assert.equal(state.weights[effect], 1 - blend);
}
clock.reset();
assert.equal(clock.advance(8).started, true);
assert.equal(clock.advance(0).started, false);
assert.equal(clock.advance(3).started, false);
const frozen = clock.state();
assert.deepEqual(clock.advance(0), { ...frozen, started: false, completed: false });
assert.equal(clock.advance(3).completed, true);
assert.equal(clock.advance(8).started, true);
assert.equal(clock.advance(4).completed, true);
assert.equal(clock.advance(8).started, true);
assert.equal(clock.advance(4).completed, true);
clock.reset();
assert.equal(clock.advance(38 * 20).effect, 'flow');
assert.equal(clock.state().phaseS, 0);
for (const dt of [-1, NaN, Infinity]) assert.throws(() => clock.advance(dt));
near(smootherstep(0), 0); near(smootherstep(1), 1);
near(smootherstep(0.25), 1 - smootherstep(0.75));
near(smootherstep(0.000001), 0);
assert.deepEqual(packSceneWeights({ particles: 0.2, flow: 0.3, wave: 0.5 }), [0.3, 0.5, 0.2]);
console.log('ok: 38-second cadence, asymmetric arrivals, smootherstep, pause, loop, one-shot handoffs');

const listeners = new Map();
let captured = null, paused = false, weights = { flow: 0.5, wave: 0.5, particles: 0 };
globalThis.FawnFormation = { screenPoint: event => [event.clientX / 100, event.clientY / 100] };
const canvas = {
  addEventListener: (type, listener) => listeners.set(type, listener),
  hasPointerCapture: id => captured === id,
  setPointerCapture: id => { captured = id; },
  releasePointerCapture: () => { captured = null; }, focus() {},
  getBoundingClientRect: () => ({ left: 0, top: 0, right: 100, bottom: 100 }),
};
const input = createCycleInteraction(canvas, { schedule() {}, isPaused: () => paused,
  weights: () => weights });
const send = (type, fields = {}) => listeners.get(type)({ type, pointerId: 1,
  pointerType: 'mouse', clientX: 50, clientY: 50, ...fields });
send('pointerdown');
assert.equal(input.pointer.press, 1);
send('pointermove', { clientX: 60, clientY: 70 });
send('pointermove', { clientX: 80, clientY: 60 });
near(input.pointer.dx, 0.3); near(input.pointer.dy, 0.1);
send('pointermove', { pointerId: 2, clientX: 0, clientY: 0 });
near(input.pointer.x, 0.8);
input.advance(0, weights); near(input.pointer.heldS, 0);
input.advance(1, weights); near(input.pointer.heldS, 0.5);
input.consume();
assert.equal(input.pointer.dx, 0); assert.equal(input.pointer.dy, 0);
assert.equal(input.pointer.press, 0); assert.equal(input.pointer.moved, false);
assert.equal(input.pointer.down, true);
send('pointerup');
const expectedCharge = 0.5 * (MAGNETIC_RELEASE.minimumCharge
  + (1 - MAGNETIC_RELEASE.minimumCharge) * 0.5 / MAGNETIC_RELEASE.chargeS);
near(input.burst.charge, expectedCharge);
input.advance(0, weights); near(input.burst.strength, expectedCharge * MAGNETIC_RELEASE.speed);
input.advance(MAGNETIC_RELEASE.durationS, weights); input.advance(0, weights);
near(input.burst.strength, 0);
input.clear();
assert.equal(input.pointer.down, false); assert.equal(input.burst.charge, 0);
send('pointerdown'); send('pointercancel');
assert.equal(input.burst.charge, 0); assert.equal(input.pointer.id, null);
paused = true;
send('pointerdown'); send('pointerup'); assert.equal(input.burst.charge, 0);
paused = false; weights = { flow: 0, wave: 0, particles: 1 };
send('pointerdown'); input.advance(1, weights); send('pointerup');
assert.equal(input.burst.charge, 0);
send('pointerdown'); input.clear();
assert.equal(captured, null); assert.equal(input.pointer.press, 0);
console.log('ok: accumulated stroke, single consumption, weighted release, pause/cancel/blur clearing');

// Local input holds the global gather while nearby parcels respond to the hand.
// Scene changes still release it, and retained standalone callers keep their old response.
const { readFileSync } = await import('node:fs');
const vm = await import('node:vm');
const sandbox = vm.createContext({});
vm.runInContext(readFileSync(new URL('../resources/fawn-formation.js', import.meta.url), 'utf8'), sandbox);
function gathered(interaction) {
  const state = sandbox.FawnFormation.transition({ delayS: 0, settleS: 0.1,
    responseRate: 4, interaction });
  state.reset(true);
  for (let i = 0; i < 240; i++) state.advance(1 / 60, true, false, false);
  return state;
}
const local = gathered('local');
const initial = local.advance(0, true, false, false);
for (let i = 0; i < 120; i++) local.advance(1 / 60, true, false, true);
near(local.advance(0, true, false, true), initial);
const standalone = gathered(undefined);
for (let i = 0; i < 120; i++) standalone.advance(1 / 60, true, true, true);
assert.ok(standalone.advance(0, true, true, true) < 0.01);
for (let i = 0; i < 240; i++) local.advance(1 / 60, true, true, false, smootherstep(i / 239));
assert.ok(local.advance(0, true, true, false) < 0.05);
for (let i = 0; i < 60; i++) local.advance(1 / 60, true, true, false, 1);
assert.ok(local.advance(0, true, true, false) < 0.005);
console.log('ok: local interaction preserves gather, scheduled blend releases, standalone response preserved');
