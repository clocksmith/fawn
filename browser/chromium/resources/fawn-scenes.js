// Scene identity and cadence. Order also defines the WGSL theme.xyz uniform layout.
const SCENES = Object.freeze({
  flow: Object.freeze({ label: 'Flows', holdS: 8, next: 'wave', transitionS: 6 }),
  wave: Object.freeze({ label: 'Magnets', holdS: 8, next: 'particles', transitionS: 4 }),
  particles: Object.freeze({ label: 'Particles', holdS: 8, next: 'flow', transitionS: 4 }),
});
const SCENE_ORDER = Object.freeze(Object.keys(SCENES));

// One set of named weights drives input, parcel forces, and surface appearance.
function sceneWeights(effect, next, blend, transitioning) {
  return Object.fromEntries(SCENE_ORDER.map((id) => [id,
    (id === effect ? 1 - blend : 0) + (transitioning && id === next ? blend : 0),
  ]));
}

// Pack only at the GPU boundary; callers use weights.flow / .wave / .particles.
function packSceneWeights(weights) {
  return SCENE_ORDER.map((id) => weights[id]);
}

export { SCENES, SCENE_ORDER, sceneWeights, packSceneWeights };
