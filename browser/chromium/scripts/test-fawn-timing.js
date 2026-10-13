import assert from 'node:assert/strict';
import { CYCLE_POLICY, createCycleClock } from '../resources/fawn-cycle-clock.js';
import '../resources/fawn-formation.js';

const { transition } = globalThis.FawnFormation;
assert.equal(CYCLE_POLICY.holdS + CYCLE_POLICY.blendS, 7);
for (const input of [false, true]) {
  for (let rotation = 0, clock = createCycleClock(); rotation < 3; rotation++) {
    for (const effect of ['flow', 'wave', 'particles']) {
      assert.equal(clock.state().effect, effect);
      assert.equal(clock.advance(4.5, input).started, true);
      const middle = clock.advance(1.25, input);
      assert.equal(middle.blend, 0.5);
      assert.ok(Math.abs(middle.interactionScale - 0.15) < 1e-12);
      assert.equal(clock.advance(1.25, input).completed, true);
    }
    assert.equal(clock.state().elapsedS, (rotation + 1) * 21);
  }
}
for (const at of [0, 2, 4.49, 4.5, 5.75, 6.99]) {
  const untouched = createCycleClock(), interacting = createCycleClock();
  untouched.advance(at); interacting.advance(at);
  for (let frame = 0; frame < 200; frame++) {
    const control = untouched.advance(0.05);
    assert.deepEqual(interacting.advance(0.05, frame % 3 === 0), control,
      'Early, late and continuous input cannot alter phase, blend or handoff');
  }
}
const curve = [];
for (let i = 0; i <= 1000; i++) {
  const clock = createCycleClock();
  curve.push(clock.advance(4.5 + 2.5 * i / 1000).interactionScale);
}
assert.equal(curve[0], 1); assert.equal(curve.at(-1), 1);
for (let i = 0; i < curve.length; i++) {
  assert.ok(curve[i] >= 0.15 - 1e-12 && curve[i] <= 1);
  assert.ok(Math.abs(curve[i] - curve[1000 - i]) < 1e-12, 'Input curve is symmetric');
  if (i > 0 && i <= 500) assert.ok(curve[i] <= curve[i - 1]);
  if (i > 500) assert.ok(curve[i] >= curve[i - 1]);
}
assert.ok(Math.abs(curve[1] - curve[0]) < 0.00001, 'No force jump entering the blend');
assert.ok(Math.abs(curve.at(-1) - curve.at(-2)) < 0.00001, 'No force jump leaving the blend');
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
const cycleFormation = transition(CYCLE_POLICY.formation); cycleFormation.reset(true);
assert.ok(step(cycleFormation, 1.9) > 0.99, 'Cycle envelope leaves time for physical gathering and dwell');
assert.throws(() => createCycleClock().advance(-1), /Invalid cycle timestep/);
console.log(JSON.stringify({ passed: true, untouchedCycleSeconds: 21, rotations: 3,
  inputIndependentCadence: true, symmetricInputAttenuation: true, uninterruptedCrossfades: true,
  formationProgressRetained: true, quietSeconds: 1, maxFormationFrameDelta: maxDelta }));
