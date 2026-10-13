// Scene cadence is independent of input; crossfades soften its force instead.
const CYCLE_POLICY = Object.freeze({
  version: 10,
  effects: Object.freeze(['flow', 'wave', 'particles']),
  labels: Object.freeze({ flow: 'Flows', wave: 'Magnets', particles: 'Particles' }),
  holdS: 6,
  blendS: 2,
  formation: Object.freeze({ delayS: 0.6, settleS: 3 }),
  transitionInputFloor: 0.15,
  maxDtS: 0.033,
  maxFrameS: 0.1,
});
function createCycleClock() {
  let elapsed = 0, phase = 0, scene = 0, prepared = false;
  const sceneS = CYCLE_POLICY.holdS + CYCLE_POLICY.blendS;
  return {
    advance(dt) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      const previousScene = scene;
      elapsed += dt;
      phase += dt;
      if (phase >= sceneS) {
        scene += Math.floor(phase / sceneS);
        phase %= sceneS;
        prepared = false;
      }
      const state = this.state();
      const started = state.transitioning && !prepared;
      if (started) prepared = true;
      return { ...state, started, completed: scene !== previousScene };
    },
    state() {
      const index = scene % CYCLE_POLICY.effects.length;
      const progress = Math.max(0, (phase - CYCLE_POLICY.holdS) / CYCLE_POLICY.blendS);
      return {
        elapsedS: elapsed,
        phaseS: phase,
        index,
        effect: CYCLE_POLICY.effects[index],
        next: CYCLE_POLICY.effects[(index + 1) % CYCLE_POLICY.effects.length],
        transitioning: phase >= CYCLE_POLICY.holdS,
        blend: progress ** 3 * (progress * (progress * 6 - 15) + 10),
        interactionScale: 1 - (1 - CYCLE_POLICY.transitionInputFloor)
          * Math.sin(Math.PI * progress) ** 2,
      };
    },
    reset() {
      elapsed = 0;
      phase = scene = 0;
      prepared = false;
    },
  };
}
export { CYCLE_POLICY, createCycleClock };
