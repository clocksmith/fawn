// One parcel population survives every effect handoff and formation change.
import * as flow from './fawn-flow-shaders.js';
import * as wave from './fawn-wave-shaders.js';
import { updateWaveSources } from './fawn-wave-shaders.js';
import { createScenePasses } from './fawn-scene-passes.js';
import { packSceneWeights } from './fawn-scenes.js';
import { PARCEL_POLICY, PARCEL_UPDATE, createParcelData } from './fawn-parcels.js';
import { createParcelRenderer } from './fawn-parcel-renderer.js';
const GPU_POLICY = Object.freeze({
  version: 14,
  maxCount: 262144,
  pressureIterations: flow.PRESSURE_ITERATIONS,
  flowGroup: flow.WORKGROUP_SIZE,
  waveGroup: wave.WORKGROUP_SIZE,
  parcelGroup: PARCEL_POLICY.workgroup,
  parcelBytes: PARCEL_POLICY.strideFloats * 4,
  hoverStrengthScale: 1 / 3,
  magneticPressScale: 0.4,
  particlePressScale: 0.33,
  pressSweepLimit: 4,
  flowPressForce: 0.7,
  magneticSpinRate: 2.25,
  magneticArrivalPower: 2,
});
function createCycleGpu(device, canvas, format, count) {
  if (!FawnFormation.WORKLOAD.cycleCounts.includes(count) || count > GPU_POLICY.maxCount)
    throw new Error(`Unsupported cycle workload: ${count}`);
  const grid = Math.ceil(Math.sqrt(count)),
    fieldCount = grid * grid,
    buffers = [];
  const buffer = (label, size, uniform = false) => {
    const b = device.createBuffer({
      label,
      size,
      usage:
        GPUBufferUsage.COPY_DST |
        GPUBufferUsage.COPY_SRC |
        (uniform ? GPUBufferUsage.UNIFORM : GPUBufferUsage.STORAGE),
    });
    buffers.push(b);
    return b;
  };
  const field = (label) => buffer(label, fieldCount * 16);
  const compute = (label, code) =>
    device.createComputePipeline({
      label,
      layout: 'auto',
      compute: { module: device.createShaderModule({ label, code }), entryPoint: 'main' },
    });
  const group = (pipeline, resources) =>
    device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: resources.map((resource, binding) => ({
        binding,
        resource: resource instanceof GPUBuffer ? { buffer: resource } : resource,
      })),
    });
  const dispatch = (pass, pipeline, bindings, surfaceGroup = null, invocations = count) => {
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, bindings);
    if (surfaceGroup)
      pass.dispatchWorkgroups(Math.ceil(grid / surfaceGroup), Math.ceil(grid / surfaceGroup));
    else pass.dispatchWorkgroups(Math.ceil(invocations / GPU_POLICY.parcelGroup));
  };
  const flowUniform = buffer('Cycle Flow uniforms', 80, true);
  const waveUniform = buffer('Cycle Wave uniforms', wave.WAVE_UNIFORM_FLOATS * 4, true);
  const parcelUniform = buffer('Cycle parcel uniforms', 80, true);
  const transferUniform = buffer('Cycle transfer uniforms', 48, true);
  const velocities = [field('Cycle velocity 0'), field('Cycle velocity 1')];
  const dyes = [field('Cycle dye 0'), field('Cycle dye 1')];
  const pressures = [
    buffer('Cycle pressure 0', fieldCount * 4),
    buffer('Cycle pressure 1', fieldCount * 4),
  ];
  const curl = field('Cycle divergence and curl');
  const waves = [field('Cycle surface 0'), field('Cycle surface 1')];
  const parcels = [
    buffer('Cycle parcels 0', count * GPU_POLICY.parcelBytes),
    buffer('Cycle parcels 1', count * GPU_POLICY.parcelBytes),
  ];
  const goals = buffer('Cycle logo targets', PARCEL_POLICY.targetGrid ** 2 * 16);
  const boundary = buffer('Cycle logo boundary', PARCEL_POLICY.targetGrid ** 2 * 16);
  const deposits = buffer('Cycle deposits', fieldCount * 24);
  const state = { dyeIndex: 0, waveIndex: 0, parcelIndex: 0 };
  let parcelGroups;
  const parcelCompute = compute('Cycle moving parcels', PARCEL_UPDATE);
  const scenes = createScenePasses({ compute, group, dispatch }, {
    flowUniform, waveUniform, transferUniform, velocities, dyes, pressures,
    curl, waves, parcels, deposits, fieldCount,
  }, state, GPU_POLICY);
  const renderer = createParcelRenderer(device, canvas, format, count, parcels, waves, grid,
    { dyes, velocity: velocities[0], curl });
  function resize() {
    const targets = FawnFormation.points(
      PARCEL_POLICY.targetGrid,
      canvas,
      document.querySelector('.brand-logo'),
    );
    device.queue.writeBuffer(goals, 0, targets);
    device.queue.writeBuffer(boundary, 0, FawnFormation.field(
      PARCEL_POLICY.targetGrid, canvas, document.querySelector('.brand-logo'),
    ));
    // Bind the populated range only: spare capacity is not a target at the origin.
    parcelGroups = [0, 1].map((p) =>
      [0, 1].map((d) =>
        [0, 1].map((w) =>
          group(parcelCompute, [
            parcelUniform,
            parcels[p],
            parcels[1 - p],
            { buffer: goals, size: targets.byteLength },
            velocities[0],
            dyes[d],
            waves[w],
            curl,
            boundary,
          ]),
        ),
      ),
    );
    renderer.resize();
  }
  function reset() {
    const seed = FawnFormation.seed(grid),
      material = createParcelData(grid, seed, count);
    dyes.forEach((b) => device.queue.writeBuffer(b, 0, seed.dye));
    velocities.forEach((b) => device.queue.writeBuffer(b, 0, seed.velocity));
    parcels.forEach((b) => device.queue.writeBuffer(b, 0, material));
    const encoder = device.createCommandEncoder();
    [...waves, ...pressures, curl, deposits].forEach((b) => encoder.clearBuffer(b));
    device.queue.submit([encoder.finish()]);
    state.dyeIndex = state.waveIndex = state.parcelIndex = 0;
    resize();
  }
  function uniforms(dt, time, shape, pointer, steps, weights, burst = null) {
    const viewport = FawnFormation.viewport(canvas);
    // The parcel shader applies the scene weight once; this is the press's per-scene character.
    const pointWeight = weights.wave + weights.particles;
    const pressScale = pointWeight > 0 ? (weights.wave * GPU_POLICY.magneticPressScale
      - weights.particles * GPU_POLICY.particlePressScale) / pointWeight : 0;
    const pressing = pointer.hover && pointer.down && !burst?.strength;
    // World-space hand velocity for a held drag, bounded so a fast flick cannot fling material.
    const hand = pressing && dt > 0 ? [pointer.dx * 2 / dt, pointer.dy * 2 / dt] : [0, 0];
    const sweep = hand.map((v) => v / Math.max(1, Math.hypot(...hand) / GPU_POLICY.pressSweepLimit));
    const force = pointer.hover ? (pointer.down ? GPU_POLICY.flowPressForce : 0.28 * GPU_POLICY.hoverStrengthScale) : 0;
    device.queue.writeBuffer(
      flowUniform,
      0,
      new Float32Array([
        grid,
        grid,
        1 / grid,
        1 / grid,
        pointer.x,
        pointer.y,
        force,
        pointer.down ? 0.1 : 0.05,
        (dt * 0.82) / steps,
        time,
        2,
        0,
        pointer.dx,
        pointer.dy,
        pointer.press,
        0,
        ...viewport,
        steps,
        0,
      ]),
    );
    const w = new Float32Array(wave.WAVE_UNIFORM_FLOATS);
    w.set([
      grid,
      grid,
      1 / grid,
      1 / grid,
      pointer.x,
      pointer.y,
      pointer.hover && !burst?.strength ? (pointer.down ? GPU_POLICY.magneticPressScale
        : 0.55 * GPU_POLICY.hoverStrengthScale) : 0,
      0,
      (dt * 1.1) / steps,
      time,
      canvas.width / canvas.height,
      0,
    ]);
    // Growing source strength slows the field's arrival without slowing its motion.
    const arrival = weights.wave ** GPU_POLICY.magneticArrivalPower;
    updateWaveSources(w, time, arrival, GPU_POLICY.magneticSpinRate);
    device.queue.writeBuffer(waveUniform, 0, w);
    device.queue.writeBuffer(
      parcelUniform,
      0,
      new Float32Array([
        grid,
        count,
        dt,
        time,
        ...packSceneWeights(weights),
        shape,
        pointer.x * 2 - 1,
        pointer.y * 2 - 1,
        pointer.hover && !burst?.strength ? (pointer.down ? 26.67 * pressScale
          : 2 * GPU_POLICY.hoverStrengthScale) : 0,
        pressing ? 1 : 0,
        (burst?.x ?? pointer.x) * 2 - 1,
        (burst?.y ?? pointer.y) * 2 - 1,
        burst?.strength ?? 0,
        burst?.radius ?? 0,
        ...viewport,
        ...sweep,
      ]),
    );
    device.queue.writeBuffer(
      transferUniform,
      0,
      new Float32Array([grid, grid, 1 / grid, 1 / grid, 0, 0, 0, 0, 0, 0, 0, 0]),
    );
  }
  function advanceParcels(encoder) {
    const pass = encoder.beginComputePass();
    dispatch(pass, parcelCompute, parcelGroups[state.parcelIndex][state.dyeIndex][state.waveIndex]);
    pass.end();
    state.parcelIndex = 1 - state.parcelIndex;
  }
  reset();
  return {
    uniforms,
    simulate: scenes.simulate,
    advanceParcels,
    prepareScene: scenes.prepareScene,
    resize,
    reset,
    draw: (encoder, view, weights, time, shimmerTime, shape) =>
      renderer.draw(encoder, view, state.parcelIndex, weights, time, shimmerTime, state.waveIndex, state.dyeIndex, shape),
    destroy() {
      renderer.destroy();
      buffers.forEach((b) => b.destroy());
    },
  };
}
export { GPU_POLICY, createCycleGpu };
