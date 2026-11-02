// Scene field passes and arrival handoffs. Resources belong to fawn-cycle-gpu.js.
// prepareScene runs once at blend start; simulate runs both neighbors during a blend.
import * as flow from './fawn-flow-shaders.js';
import * as wave from './fawn-wave-shaders.js';
import { FLOW_TO_WAVE, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT } from './fawn-cycle-transfers.js';
function createScenePasses({ compute, group, dispatch }, fields, state, policy) {
  const { flowUniform, waveUniform, transferUniform, velocities, dyes, pressures,
    curl, waves, parcels, deposits, fieldCount } = fields;
  const flowCompute = Object.fromEntries(Object.entries({
    advect: 'ADVECT_VEL_WGSL', curl: 'DIV_CURL_WGSL', pressure: 'PRESSURE_WGSL',
    project: 'PROJECT_WGSL', dye: 'ADVECT_DYE_WGSL',
  }).map(([pass, name]) => [pass, compute(`Cycle Flow ${name}`, flow[name])]));
  const flowGroups = {
    advect: group(flowCompute.advect, [flowUniform, velocities[0], velocities[1]]),
    curl: group(flowCompute.curl, [flowUniform, velocities[1], curl]),
    pressure: [0, 1].map((i) =>
      group(flowCompute.pressure, [flowUniform, pressures[i], curl, pressures[1 - i]]),
    ),
    project: group(flowCompute.project, [flowUniform, velocities[1], pressures[0], curl, velocities[0]]),
    dye: [0, 1].map((i) =>
      group(flowCompute.dye, [flowUniform, dyes[i], velocities[0], dyes[1 - i]]),
    ),
  };
  const waveCompute = compute('Cycle Wave solver', wave.COMPUTE_WGSL);
  const waveGroups = [0, 1].map((i) => group(waveCompute, [waveUniform, waves[i], waves[1 - i]]));
  const transfers = [FLOW_TO_WAVE, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT].map((code, i) =>
    compute(`Cycle handoff ${i}`, code),
  );
  const handoffGroups = {
    wave: [0, 1].map((i) =>
      group(transfers[0], [transferUniform, dyes[i], velocities[0], waves[0]]),
    ),
    deposit: [0, 1].map((i) => group(transfers[1], [transferUniform, parcels[i], deposits])),
    resolve: group(transfers[2], [transferUniform, deposits, dyes[0], velocities[0]]),
  };
  // Lifecycle belongs to the arrival scene. No scene resets the shared parcels.
  const scenes = {
    flow: {
      prepare(encoder) {
        encoder.clearBuffer(deposits);
        const pass = encoder.beginComputePass();
        dispatch(pass, transfers[1], handoffGroups.deposit[state.parcelIndex]);
        dispatch(pass, transfers[2], handoffGroups.resolve, null, fieldCount);
        state.dyeIndex = 0;
        pass.end();
        pressures.forEach((buffer) => encoder.clearBuffer(buffer));
        encoder.clearBuffer(curl);
      },
      simulate(encoder, steps) {
        const pass = encoder.beginComputePass();
        for (let step = 0; step < steps; step++) {
          dispatch(pass, flowCompute.advect, flowGroups.advect, policy.flowGroup);
          dispatch(pass, flowCompute.curl, flowGroups.curl, policy.flowGroup);
          for (let i = 0; i < policy.pressureIterations; i++)
            dispatch(pass, flowCompute.pressure, flowGroups.pressure[i % 2], policy.flowGroup);
          dispatch(pass, flowCompute.project, flowGroups.project, policy.flowGroup);
          dispatch(pass, flowCompute.dye, flowGroups.dye[state.dyeIndex], policy.flowGroup);
          state.dyeIndex = 1 - state.dyeIndex;
        }
        pass.end();
      },
    },
    wave: {
      prepare(encoder) {
        const pass = encoder.beginComputePass();
        dispatch(pass, transfers[0], handoffGroups.wave[state.dyeIndex], null, fieldCount);
        state.waveIndex = 0;
        pass.end();
      },
      simulate(encoder, steps) {
        const pass = encoder.beginComputePass();
        for (let i = 0; i < steps; i++) {
          dispatch(pass, waveCompute, waveGroups[state.waveIndex], policy.waveGroup);
          state.waveIndex = 1 - state.waveIndex;
        }
        pass.end();
      },
    },
    particles: {
      // No field copy or solver: the parcels already carry position, velocity and pigment.
      prepare() {},
      simulate() {},
    },
  };
  return {
    simulate: (encoder, effect, steps) => scenes[effect].simulate(encoder, steps),
    prepareScene: (encoder, destination) => scenes[destination].prepare(encoder),
  };
}
export { createScenePasses };
