// One visible-time clock; interaction holds both the schedule and an in-progress blend.
const CYCLE_POLICY = Object.freeze({
  version: 1,
  effects: Object.freeze(['flow', 'wave', 'particles']),
  holdS: 10,
  blendS: 4,
  idleS: 1,
  maxDtS: 0.033,
});
function createCycleClock() {
  let index = 0,
    age = 0,
    progress = 0,
    idle = CYCLE_POLICY.idleS;
  return {
    advance(dt, interacting = false) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      idle = interacting ? 0 : idle + dt;
      let started = false,
        completed = false;
      if (idle >= CYCLE_POLICY.idleS) {
        if (age < CYCLE_POLICY.holdS) {
          age += dt;
          if (age >= CYCLE_POLICY.holdS) started = true;
        } else {
          progress = Math.min(1, progress + dt / CYCLE_POLICY.blendS);
          if (progress === 1) {
            index = (index + 1) % CYCLE_POLICY.effects.length;
            age = 0;
            progress = 0;
            completed = true;
          }
        }
      }
      return { ...this.state(), started, completed };
    },
    state() {
      return {
        index,
        effect: CYCLE_POLICY.effects[index],
        next: CYCLE_POLICY.effects[(index + 1) % CYCLE_POLICY.effects.length],
        transitioning: age >= CYCLE_POLICY.holdS,
        blend: progress * progress * (3 - 2 * progress),
      };
    },
    reset() {
      index = 0;
      age = 0;
      progress = 0;
      idle = CYCLE_POLICY.idleS;
    },
  };
}
export { CYCLE_POLICY, createCycleClock };
