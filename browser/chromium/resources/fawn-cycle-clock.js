// Scene time advances independently of input; the host pauses it for pause/hidden tabs.
const CYCLE_POLICY = Object.freeze({
  version: 2,
  effects: Object.freeze(['flow', 'wave', 'particles']),
  labels: Object.freeze({ flow: 'Flows', wave: 'Magnets', particles: 'Particles' }),
  holdS: 10,
  blendS: 4,
  maxDtS: 0.033,
});
function createCycleClock() {
  let elapsed = 0;
  const sceneS = CYCLE_POLICY.holdS + CYCLE_POLICY.blendS;
  return {
    advance(dt) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      const previousScene = Math.floor(elapsed / sceneS);
      const previousPhase = elapsed % sceneS;
      elapsed += dt;
      const completed = Math.floor(elapsed / sceneS) !== previousScene;
      const started =
        elapsed % sceneS >= CYCLE_POLICY.holdS && (previousPhase < CYCLE_POLICY.holdS || completed);
      return { ...this.state(), started, completed };
    },
    state() {
      const index = Math.floor(elapsed / sceneS) % CYCLE_POLICY.effects.length;
      const phase = elapsed % sceneS;
      const progress = Math.max(0, (phase - CYCLE_POLICY.holdS) / CYCLE_POLICY.blendS);
      return {
        index,
        effect: CYCLE_POLICY.effects[index],
        next: CYCLE_POLICY.effects[(index + 1) % CYCLE_POLICY.effects.length],
        transitioning: phase >= CYCLE_POLICY.holdS,
        blend: progress * progress * (3 - 2 * progress),
      };
    },
    reset() {
      elapsed = 0;
    },
  };
}
export { CYCLE_POLICY, createCycleClock };
