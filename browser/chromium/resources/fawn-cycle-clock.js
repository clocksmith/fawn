// Scene cadence is independent of input. Each scene shows its full field before the mark
// gathers late in the hold; input keeps each scene's response through every blend.
import { SCENES, SCENE_ORDER, sceneWeights } from './fawn-scenes.js';
const CYCLE_POLICY = Object.freeze({
  version: 13,
  formation: Object.freeze({ delayS: 3, settleS: 2.5, responseRate: 4, interaction: 'local' }),
  maxDtS: 0.033,
  maxFrameS: 0.1,
});
// Quintic smootherstep: zero velocity and acceleration at each end of the lerp.
function smootherstep(progress) {
  return progress ** 3 * (progress * (progress * 6 - 15) + 10);
}
function createCycleClock() {
  let elapsed = 0, phase = 0, scene = 0, prepared = false;
  const cycleDuration = SCENE_ORDER.reduce((total, id) =>
    total + SCENES[id].holdS + SCENES[id].transitionS, 0);
  const currentScene = () => SCENES[SCENE_ORDER[scene % SCENE_ORDER.length]];
  return {
    advance(dt) {
      if (!Number.isFinite(dt) || dt < 0) throw new Error(`Invalid cycle timestep: ${dt}`);
      const previousScene = scene;
      elapsed += dt;
      phase += dt;
      const cycles = Math.floor(phase / cycleDuration);
      if (cycles > 0) {
        phase -= cycles * cycleDuration;
        scene += cycles * SCENE_ORDER.length;
        prepared = false;
      }
      while (phase >= currentScene().holdS + currentScene().transitionS) {
        phase -= currentScene().holdS + currentScene().transitionS;
        scene++;
        prepared = false;
      }
      const state = this.state();
      const started = state.transitioning && !prepared;
      if (started) prepared = true;
      return { ...state, started, completed: scene !== previousScene };
    },
    state() {
      const index = scene % SCENE_ORDER.length;
      const effect = SCENE_ORDER[index];
      const { next, holdS, transitionS } = currentScene();
      const progress = Math.max(0, (phase - holdS) / transitionS);
      const transitioning = phase >= holdS;
      const blend = smootherstep(progress);
      return {
        elapsedS: elapsed,
        phaseS: phase,
        index,
        effect,
        next,
        transitioning,
        blend,
        weights: sceneWeights(effect, next, blend, transitioning),
      };
    },
    reset() {
      elapsed = 0;
      phase = scene = 0;
      prepared = false;
    },
  };
}
export { CYCLE_POLICY, smootherstep, createCycleClock };
