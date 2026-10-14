// Scene cadence is independent of input. Each scene shows its full field before the mark
// gathers late in the hold; input keeps each scene's response through every blend.
const CYCLE_POLICY = Object.freeze({
  version: 12,
  effects: Object.freeze(['flow', 'wave', 'particles']),
  labels: Object.freeze({ flow: 'Flows', wave: 'Magnets', particles: 'Particles' }),
  holdS: 8,
  blendS: 4,
  arrivalBlendS: Object.freeze({ wave: 6 }),
  formation: Object.freeze({ delayS: 3, settleS: 2.5, responseRate: 4 }),
  maxDtS: 0.033,
  maxFrameS: 0.1,
});
function createCycleClock() {
  let elapsed = 0, phase = 0, scene = 0, prepared = false;
  const cycleDuration = CYCLE_POLICY.effects.reduce((total, effect) => total + CYCLE_POLICY.holdS
    + (CYCLE_POLICY.arrivalBlendS[effect] ?? CYCLE_POLICY.blendS), 0);
  const blendDuration = () => {
    const next = CYCLE_POLICY.effects[(scene + 1) % CYCLE_POLICY.effects.length];
    return CYCLE_POLICY.arrivalBlendS[next] ?? CYCLE_POLICY.blendS;
  };
  return {
    advance(dt) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      const previousScene = scene;
      elapsed += dt;
      phase += dt;
      const cycles = Math.floor(phase / cycleDuration);
      if (cycles > 0) {
        phase -= cycles * cycleDuration;
        scene += cycles * CYCLE_POLICY.effects.length;
        prepared = false;
      }
      while (phase >= CYCLE_POLICY.holdS + blendDuration()) {
        phase -= CYCLE_POLICY.holdS + blendDuration();
        scene++;
        prepared = false;
      }
      const state = this.state();
      const started = state.transitioning && !prepared;
      if (started) prepared = true;
      return { ...state, started, completed: scene !== previousScene };
    },
    state() {
      const index = scene % CYCLE_POLICY.effects.length;
      const progress = Math.max(0, (phase - CYCLE_POLICY.holdS) / blendDuration());
      return {
        elapsedS: elapsed,
        phaseS: phase,
        index,
        effect: CYCLE_POLICY.effects[index],
        next: CYCLE_POLICY.effects[(index + 1) % CYCLE_POLICY.effects.length],
        transitioning: phase >= CYCLE_POLICY.holdS,
        blend: progress ** 3 * (progress * (progress * 6 - 15) + 10),
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
