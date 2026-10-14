// One parcel population survives every effect handoff and formation change.
import { flow, wave, updateWaveSources } from './fawn-shaders.js';
import { FLOW_TO_WAVE, PARTICLE_DEPOSIT, RESOLVE_DEPOSIT } from './fawn-cycle-transfers.js';
import { PARCEL_POLICY, PARCEL_UPDATE, createParcelData } from './fawn-parcels.js';
import { createParcelRenderer } from './fawn-parcel-renderer.js';
const GPU_POLICY = Object.freeze({
  version: 13,
  maxCount: 262144,
  pressureIterations: flow.PRESSURE_ITERATIONS,
  flowGroup: flow.WORKGROUP_SIZE,
  waveGroup: wave.WORKGROUP_SIZE,
  parcelGroup: PARCEL_POLICY.workgroup,
  parcelBytes: PARCEL_POLICY.strideFloats * 4,
  hoverStrengthScale: 1 / 3,
  magneticPressScale: 0.18,
  particlePressScale: 0.13,
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
  const parcelUniform = buffer('Cycle parcel uniforms', 64, true);
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
  let dyeIndex = 0,
    waveIndex = 0,
    parcelIndex = 0,
    parcelGroups;
  const flowCompute = [
    'ADVECT_VEL_WGSL',
    'DIV_CURL_WGSL',
    'PRESSURE_WGSL',
    'PROJECT_WGSL',
    'ADVECT_DYE_WGSL',
  ].map((name) => compute(`Cycle Flow ${name}`, flow[name]));
  const flowGroups = {
    advect: group(flowCompute[0], [flowUniform, velocities[0], velocities[1]]),
    curl: group(flowCompute[1], [flowUniform, velocities[1], curl]),
    pressure: [0, 1].map((i) =>
      group(flowCompute[2], [flowUniform, pressures[i], curl, pressures[1 - i]]),
    ),
    project: group(flowCompute[3], [flowUniform, velocities[1], pressures[0], curl, velocities[0]]),
    dye: [0, 1].map((i) =>
      group(flowCompute[4], [flowUniform, dyes[i], velocities[0], dyes[1 - i]]),
    ),
  };
  const waveCompute = compute('Cycle Wave solver', wave.COMPUTE_WGSL);
  const waveGroups = [0, 1].map((i) => group(waveCompute, [waveUniform, waves[i], waves[1 - i]]));
  const parcelCompute = compute('Cycle moving parcels', PARCEL_UPDATE);
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
    dyeIndex = waveIndex = parcelIndex = 0;
    resize();
  }
  function uniforms(dt, time, shape, pointer, steps, weights, burst = null) {
    const viewport = FawnFormation.viewport(canvas);
    // The parcel shader applies the scene weight once; this is the press's per-scene character.
    const pointWeight = weights[1] + weights[2];
    const pressScale = pointWeight > 0 ? (weights[1] * GPU_POLICY.magneticPressScale
      - weights[2] * GPU_POLICY.particlePressScale) / pointWeight : 0;
    const pressing = pointer.hover && pointer.down && !burst?.strength;
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
    const arrival = weights[1] ** GPU_POLICY.magneticArrivalPower;
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
        ...weights,
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
      ]),
    );
    device.queue.writeBuffer(
      transferUniform,
      0,
      new Float32Array([grid, grid, 1 / grid, 1 / grid, 0, 0, 0, 0, 0, 0, 0, 0]),
    );
  }
  function simulate(encoder, effect, steps) {
    if (effect === 'particles') return;
    const pass = encoder.beginComputePass();
    if (effect === 'flow')
      for (let step = 0; step < steps; step++) {
        dispatch(pass, flowCompute[0], flowGroups.advect, GPU_POLICY.flowGroup);
        dispatch(pass, flowCompute[1], flowGroups.curl, GPU_POLICY.flowGroup);
        for (let i = 0; i < GPU_POLICY.pressureIterations; i++)
          dispatch(pass, flowCompute[2], flowGroups.pressure[i % 2], GPU_POLICY.flowGroup);
        dispatch(pass, flowCompute[3], flowGroups.project, GPU_POLICY.flowGroup);
        dispatch(pass, flowCompute[4], flowGroups.dye[dyeIndex], GPU_POLICY.flowGroup);
        dyeIndex = 1 - dyeIndex;
      }
    else
      for (let i = 0; i < steps; i++) {
        dispatch(pass, waveCompute, waveGroups[waveIndex], GPU_POLICY.waveGroup);
        waveIndex = 1 - waveIndex;
      }
    pass.end();
  }
  function advanceParcels(encoder) {
    const pass = encoder.beginComputePass();
    dispatch(pass, parcelCompute, parcelGroups[parcelIndex][dyeIndex][waveIndex]);
    pass.end();
    parcelIndex = 1 - parcelIndex;
  }
  function handoff(encoder, effect) {
    // Wave -> Particles changes the field weights only; every parcel stays resident.
    if (effect === 'wave') return;
    if (effect === 'particles') encoder.clearBuffer(deposits);
    const pass = encoder.beginComputePass();
    if (effect === 'flow') {
      dispatch(pass, transfers[0], handoffGroups.wave[dyeIndex], null, fieldCount);
      waveIndex = 0;
    } else {
      dispatch(pass, transfers[1], handoffGroups.deposit[parcelIndex]);
      dispatch(pass, transfers[2], handoffGroups.resolve, null, fieldCount);
      dyeIndex = 0;
    }
    pass.end();
    if (effect === 'particles') {
      pressures.forEach((b) => encoder.clearBuffer(b));
      encoder.clearBuffer(curl);
    }
  }
  reset();
  return {
    uniforms,
    simulate,
    advanceParcels,
    handoff,
    resize,
    reset,
    draw: (encoder, view, weights, time, shimmerTime, shape) =>
      renderer.draw(encoder, view, parcelIndex, weights, time, shimmerTime, waveIndex, dyeIndex, shape),
    destroy() {
      renderer.destroy();
      buffers.forEach((b) => b.destroy());
    },
  };
}
export { GPU_POLICY, createCycleGpu };
