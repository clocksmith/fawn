// Input caps late scene time without resetting early progress or the visible blend.
const CYCLE_POLICY = Object.freeze({
  version: 5,
  effects: Object.freeze(['flow', 'wave', 'particles']),
  labels: Object.freeze({ flow: 'Flows', wave: 'Magnets', particles: 'Particles' }),
  holdS: 11,
  blendS: 5,
  interactionQuietS: 1,
  maxDtS: 0.033,
  maxFrameS: 0.1,
});
function createCycleClock() {
  let elapsed = 0, phase = 0, scene = 0, blendFloor = 0, prepared = false;
  const sceneS = CYCLE_POLICY.holdS + CYCLE_POLICY.blendS;
  return {
    advance(dt, interacting = false) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      const previousScene = scene;
      const heldBlend = this.state().blend;
      elapsed += dt;
      phase += dt;
      const latest = CYCLE_POLICY.holdS - CYCLE_POLICY.interactionQuietS;
      if (interacting && phase > latest) {
        phase = latest;
        // Keep an already visible transition in place while its clock is deferred.
        blendFloor = heldBlend;
      } else if (phase >= sceneS) {
        scene += Math.floor(phase / sceneS);
        phase %= sceneS;
        blendFloor = 0;
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
        transitioning: phase >= CYCLE_POLICY.holdS || blendFloor > 0,
        blend: blendFloor + (1 - blendFloor)
          * progress ** 3 * (progress * (progress * 6 - 15) + 10),
      };
    },
    reset() {
      elapsed = 0;
      phase = scene = blendFloor = 0;
      prepared = false;
    },
  };
}
export { CYCLE_POLICY, createCycleClock };
