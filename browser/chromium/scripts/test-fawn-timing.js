import assert from 'node:assert/strict';
import { CYCLE_POLICY, createCycleClock } from '../resources/fawn-cycle-clock.js';
import '../resources/fawn-formation.js';

const { transition } = globalThis.FawnFormation;
assert.equal(3 * (CYCLE_POLICY.holdS + CYCLE_POLICY.blendS), 24);
for (let rotation = 0, clock = createCycleClock(); rotation < 3; rotation++) {
  for (const effect of ['flow', 'wave', 'particles']) {
    assert.equal(clock.state().effect, effect);
    assert.equal(clock.advance(6).started, true);
    assert.equal(clock.advance(1).blend, 0.5);
    assert.equal(clock.advance(1).completed, true);
  }
  assert.equal(clock.state().elapsedS, (rotation + 1) * 24);
}
for (const at of [0, 2, 4.9, 5, 5.5, 5.99]) {
  const clock = createCycleClock();
  clock.advance(at);
  assert.equal(clock.advance(0, true).phaseS, Math.min(at, 5));
  for (let i = 0; i < 100; i++) assert.ok(clock.advance(0.1, true).phaseS <= 5);
  assert.equal(clock.advance(0.99).transitioning, false);
  assert.equal(clock.advance(0.02).started, true);
}
for (const at of [6, 6.01, 6.5, 7, 7.99]) {
  const clock = createCycleClock(); clock.advance(at);
  const before = clock.state();
  const next = clock.advance(0.005, true);
  assert.ok(next.blend >= before.blend);
  assert.equal(next.started, false);
  assert.equal(clock.advance(8 - next.phaseS, true).completed, true);
}
const step = (f, seconds, active = false, interacting = active, blend = 0) => {
  let value;
  for (let i = 0; i < Math.round(seconds * 100); i++)
    value = f.advance(0.01, true, active, interacting, blend);
  return value;
};
const f = transition(); f.reset(true);
assert.ok(step(f, 0.8) < 1e-12);
assert.ok(step(f, 3.5) > 0.99);
step(f, 2, true);
assert.ok(step(f, 0.99) < 0.001, 'Quiet guard holds formation released');
assert.ok(step(f, 0.6) > 0.9, 'Retained progress resumes without another three-second ramp');
const partly = transition(); partly.reset(true);
step(partly, 2.3); step(partly, 2, true); step(partly, 1);
assert.ok(step(partly, 0.6) > 0.5, 'Partial formation progress survives interaction');
const beforePause = partly.advance(0, true, false);
assert.equal(partly.advance(0, true, true), beforePause);
// Crossfade release and next-scene retarget preserve value/velocity continuity.
let previous = step(f, 1), maxDelta = 0;
for (let i = 0; i <= 200; i++) {
  const t = i / 200, blend = t ** 3 * (t * (t * 6 - 15) + 10);
  const value = f.advance(0.01, true, true, false, blend);
  maxDelta = Math.max(maxDelta, Math.abs(value - previous)); previous = value;
}
f.retarget();
const next = f.advance(0.01, true, false);
assert.ok(Math.abs(next - previous) < 0.01);
assert.ok(maxDelta < 0.02);
assert.throws(() => createCycleClock().advance(-1), /Invalid cycle timestep/);
console.log(JSON.stringify({ passed: true, untouchedCycleSeconds: 24, rotations: 3,
  earlyAndLateInteraction: true, uninterruptedCrossfades: true,
  formationProgressRetained: true, quietSeconds: 1, maxFormationFrameDelta: maxDelta }));
